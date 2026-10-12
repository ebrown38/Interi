// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Assert.h>
#include <interi/core/Hash.h>
#include <interi/core/memory/Allocator.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace interi
{

/// Open-addressing hash map with Robin Hood probing and backward-shift deletion (no tombstones), stored in one
/// flat array drawn from an IAllocator. Lookups touch consecutive slots, which keeps them cache-friendly.
///
/// Iteration yields Entry& with `key` and `value` members (structured bindings work). Do not modify `key`.
/// Insert, Erase and Clear invalidate pointers and iterators. Find accepts any type the hasher and KeyEqual
/// accept when the hasher is transparent (e.g. std::string_view for std::string keys).
template <typename K, typename V, typename Hash = Hasher<K>, typename KeyEqual = std::equal_to<>>
class HashMap
{
public:
    struct Entry
    {
        K key;
        V value;
    };

    static constexpr float kMaxLoadFactor = 0.875f;

    template <bool Const>
    class Iterator
    {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = Entry;
        using difference_type = std::ptrdiff_t;
        using reference = std::conditional_t<Const, const Entry&, Entry&>;
        using pointer = std::conditional_t<Const, const Entry*, Entry*>;
        using MapPointer = std::conditional_t<Const, const HashMap*, HashMap*>;

        Iterator() noexcept = default;
        Iterator(MapPointer map, std::size_t index) noexcept
            : m_map(map)
            , m_index(index)
        {
            SkipEmpty();
        }

        reference operator*() const noexcept { return m_map->m_entries[m_index]; }
        pointer operator->() const noexcept { return &m_map->m_entries[m_index]; }
        Iterator& operator++() noexcept
        {
            ++m_index;
            SkipEmpty();
            return *this;
        }
        Iterator operator++(int) noexcept
        {
            Iterator previous = *this;
            ++*this;
            return previous;
        }
        friend bool operator==(const Iterator& a, const Iterator& b) noexcept { return a.m_index == b.m_index; }

    private:
        void SkipEmpty() noexcept
        {
            while (m_index < m_map->m_capacity && m_map->m_distances[m_index] == 0)
            {
                ++m_index;
            }
        }

        MapPointer m_map = nullptr;
        std::size_t m_index = 0;
    };

    using iterator = Iterator<false>;
    using const_iterator = Iterator<true>;

    explicit HashMap(IAllocator& allocator = GetDefaultAllocator()) noexcept
        : m_allocator(&allocator)
    {
    }
    HashMap(const HashMap& other)
        : m_allocator(other.m_allocator)
    {
        if (other.m_size > 0)
        {
            Allocate(other.m_capacity);
            for (const Entry& entry : other)
            {
                InsertNew(Entry{entry.key, entry.value});
            }
        }
    }
    HashMap(HashMap&& other) noexcept
        : m_allocator(other.m_allocator)
        , m_entries(std::exchange(other.m_entries, nullptr))
        , m_distances(std::exchange(other.m_distances, nullptr))
        , m_capacity(std::exchange(other.m_capacity, 0))
        , m_size(std::exchange(other.m_size, 0))
    {
    }
    HashMap& operator=(const HashMap& other)
    {
        if (this != &other)
        {
            HashMap copy(other);
            *this = std::move(copy);
        }
        return *this;
    }
    HashMap& operator=(HashMap&& other) noexcept
    {
        if (this != &other)
        {
            Destroy();
            m_allocator = other.m_allocator;
            m_entries = std::exchange(other.m_entries, nullptr);
            m_distances = std::exchange(other.m_distances, nullptr);
            m_capacity = std::exchange(other.m_capacity, 0);
            m_size = std::exchange(other.m_size, 0);
        }
        return *this;
    }
    ~HashMap() { Destroy(); }

    /// Inserts if the key is absent. Returns the value for the key and whether it was inserted; an existing
    /// value is left unchanged.
    template <typename KeyArg, typename... Args>
    std::pair<V*, bool> Insert(KeyArg&& key, Args&&... valueArgs)
    {
        if (V* existing = Find(key))
        {
            return {existing, false};
        }
        if (static_cast<float>(m_size + 1) > static_cast<float>(m_capacity) * kMaxLoadFactor)
        {
            Rehash(m_capacity == 0 ? kMinCapacity : m_capacity * 2);
        }
        Entry* placed = InsertNew(Entry{K(std::forward<KeyArg>(key)), V(std::forward<Args>(valueArgs)...)});
        return {&placed->value, true};
    }

    /// The value for `key`, default-constructing it if absent.
    template <typename KeyArg>
    V& operator[](KeyArg&& key)
    {
        return *Insert(std::forward<KeyArg>(key)).first;
    }

    template <typename Query>
    [[nodiscard]] V* Find(const Query& key) noexcept
    {
        const std::size_t index = IndexOf(key);
        return index == kNotFound ? nullptr : &m_entries[index].value;
    }
    template <typename Query>
    [[nodiscard]] const V* Find(const Query& key) const noexcept
    {
        const std::size_t index = IndexOf(key);
        return index == kNotFound ? nullptr : &m_entries[index].value;
    }
    template <typename Query>
    [[nodiscard]] bool Contains(const Query& key) const noexcept
    {
        return IndexOf(key) != kNotFound;
    }

    /// Removes the key; returns whether it was present.
    template <typename Query>
    bool Erase(const Query& key)
    {
        std::size_t index = IndexOf(key);
        if (index == kNotFound)
        {
            return false;
        }
        std::destroy_at(&m_entries[index]);
        // Backward shift: pull each following displaced entry one slot closer to its home.
        std::size_t next = (index + 1) & Mask();
        while (m_distances[next] > 1)
        {
            ::new (static_cast<void*>(&m_entries[index])) Entry(std::move(m_entries[next]));
            std::destroy_at(&m_entries[next]);
            m_distances[index] = static_cast<std::uint16_t>(m_distances[next] - 1);
            index = next;
            next = (next + 1) & Mask();
        }
        m_distances[index] = 0;
        --m_size;
        return true;
    }

    /// Removes every entry but keeps the capacity.
    void Clear() noexcept
    {
        for (std::size_t i = 0; i < m_capacity; ++i)
        {
            if (m_distances[i] != 0)
            {
                std::destroy_at(&m_entries[i]);
                m_distances[i] = 0;
            }
        }
        m_size = 0;
    }

    /// Grows so that `count` entries fit without rehashing.
    void Reserve(std::size_t count)
    {
        const auto needed = static_cast<std::size_t>(static_cast<float>(count) / kMaxLoadFactor) + 1;
        if (needed > m_capacity)
        {
            Rehash(std::bit_ceil(std::max(needed, kMinCapacity)));
        }
    }

    [[nodiscard]] std::size_t Size() const noexcept { return m_size; }
    [[nodiscard]] bool Empty() const noexcept { return m_size == 0; }
    [[nodiscard]] std::size_t Capacity() const noexcept { return m_capacity; }
    [[nodiscard]] float LoadFactor() const noexcept
    {
        return m_capacity == 0 ? 0.0f : static_cast<float>(m_size) / static_cast<float>(m_capacity);
    }

    [[nodiscard]] iterator begin() noexcept { return iterator(this, 0); }
    [[nodiscard]] iterator end() noexcept { return iterator(this, m_capacity); }
    [[nodiscard]] const_iterator begin() const noexcept { return const_iterator(this, 0); }
    [[nodiscard]] const_iterator end() const noexcept { return const_iterator(this, m_capacity); }

private:
    static constexpr std::size_t kMinCapacity = 16;
    static constexpr std::size_t kNotFound = ~std::size_t{0};
    /// Probe distance + 1 per slot; 0 marks an empty slot.
    static constexpr std::uint16_t kMaxDistance = 0xFFFF;

    [[nodiscard]] std::size_t Mask() const noexcept { return m_capacity - 1; }

    template <typename Query>
    [[nodiscard]] std::size_t IndexOf(const Query& key) const noexcept
    {
        if (m_size == 0)
        {
            return kNotFound;
        }
        std::size_t index = static_cast<std::size_t>(Hash{}(key)) & Mask();
        for (std::uint32_t distance = 1;; ++distance)
        {
            // Robin Hood invariant: once we pass an entry closer to its home than we are, the key is absent.
            if (m_distances[index] < distance)
            {
                return kNotFound;
            }
            if (KeyEqual{}(m_entries[index].key, key))
            {
                return index;
            }
            index = (index + 1) & Mask();
        }
    }

    /// Places an entry whose key is known to be absent, with capacity available. Returns where it landed.
    Entry* InsertNew(Entry&& entry)
    {
        Entry carried = std::move(entry);
        Entry* placed = nullptr;
        std::size_t index = static_cast<std::size_t>(Hash{}(carried.key)) & Mask();
        std::uint32_t distance = 1;
        for (;;)
        {
            INTERI_ASSERT(distance < kMaxDistance, "HashMap probe sequence too long: the hasher is degenerate");
            if (m_distances[index] == 0)
            {
                ::new (static_cast<void*>(&m_entries[index])) Entry(std::move(carried));
                m_distances[index] = static_cast<std::uint16_t>(distance);
                ++m_size;
                return placed != nullptr ? placed : &m_entries[index];
            }
            if (m_distances[index] < distance)
            {
                // Take from the rich: the resident is closer to home, so it moves on instead.
                std::swap(carried, m_entries[index]);
                const auto residentDistance = m_distances[index];
                m_distances[index] = static_cast<std::uint16_t>(distance);
                distance = residentDistance;
                if (placed == nullptr)
                {
                    placed = &m_entries[index];
                }
            }
            index = (index + 1) & Mask();
            ++distance;
        }
    }

    void Allocate(std::size_t capacity)
    {
        INTERI_ASSERT(std::has_single_bit(capacity));
        const std::size_t entryBytes = AlignUp(capacity * sizeof(Entry), alignof(std::uint16_t));
        void* memory = m_allocator->Allocate(entryBytes + capacity * sizeof(std::uint16_t), alignof(Entry));
        if (memory == nullptr)
        {
            throw std::bad_alloc();
        }
        m_entries = static_cast<Entry*>(memory);
        m_distances = reinterpret_cast<std::uint16_t*>(static_cast<std::byte*>(memory) + entryBytes);
        std::fill_n(m_distances, capacity, std::uint16_t{0});
        m_capacity = capacity;
    }

    void Rehash(std::size_t newCapacity)
    {
        Entry* oldEntries = m_entries;
        std::uint16_t* oldDistances = m_distances;
        const std::size_t oldCapacity = m_capacity;
        Allocate(newCapacity);
        m_size = 0;
        for (std::size_t i = 0; i < oldCapacity; ++i)
        {
            if (oldDistances[i] != 0)
            {
                InsertNew(std::move(oldEntries[i]));
                std::destroy_at(&oldEntries[i]);
            }
        }
        if (oldEntries != nullptr)
        {
            m_allocator->Free(oldEntries, AllocationBytes(oldCapacity));
        }
    }

    [[nodiscard]] static std::size_t AllocationBytes(std::size_t capacity) noexcept
    {
        return AlignUp(capacity * sizeof(Entry), alignof(std::uint16_t)) + capacity * sizeof(std::uint16_t);
    }

    void Destroy() noexcept
    {
        if (m_entries != nullptr)
        {
            Clear();
            m_allocator->Free(m_entries, AllocationBytes(m_capacity));
            m_entries = nullptr;
            m_distances = nullptr;
            m_capacity = 0;
        }
    }

    IAllocator* m_allocator;
    Entry* m_entries = nullptr;
    std::uint16_t* m_distances = nullptr;
    std::size_t m_capacity = 0;
    std::size_t m_size = 0;
};

} // namespace interi
