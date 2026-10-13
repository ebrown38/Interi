// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>

#include <string>
#include <type_traits>

namespace interi
{

/// An owned handle to a shared library (.so / .dll) loaded at runtime, closed on destruction. Move-only.
/// Used by the gameplay module host for hot reload.
class DynamicLibrary
{
public:
    DynamicLibrary() noexcept = default;
    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;
    DynamicLibrary(DynamicLibrary&& other) noexcept;
    DynamicLibrary& operator=(DynamicLibrary&& other) noexcept;
    ~DynamicLibrary();

    /// Loads the library at `path` (UTF-8). NotFound with the loader's message when it can't be loaded.
    [[nodiscard]] static Result<DynamicLibrary> Open(const std::string& path);

    /// Address of an exported symbol, or nullptr if the library doesn't export it.
    [[nodiscard]] void* FindSymbol(const char* name) const noexcept;

    /// FindSymbol cast to a function pointer type, e.g. `FindFunction<int (*)(int)>("Fn")`.
    template <typename FunctionPointer>
        requires std::is_pointer_v<FunctionPointer> && std::is_function_v<std::remove_pointer_t<FunctionPointer>>
    [[nodiscard]] FunctionPointer FindFunction(const char* name) const noexcept
    {
        return reinterpret_cast<FunctionPointer>(FindSymbol(name)); // NOLINT: object → function pointer
    }

    [[nodiscard]] bool IsOpen() const noexcept { return m_handle != nullptr; }
    void Close() noexcept;

private:
    explicit DynamicLibrary(void* handle) noexcept
        : m_handle(handle)
    {
    }

    void* m_handle = nullptr;
};

} // namespace interi
