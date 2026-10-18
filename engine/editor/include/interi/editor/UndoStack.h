// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

namespace interi::editor
{

/// An undoable edit. Do applies it (also on redo), Undo reverts it. Both must succeed whenever the stack calls them:
/// commands run in strict order, so the state they expect is always the one they left.
class ICommand
{
public:
    ICommand() = default;
    ICommand(const ICommand&) = delete;
    ICommand& operator=(const ICommand&) = delete;
    ICommand(ICommand&&) = delete;
    ICommand& operator=(ICommand&&) = delete;
    virtual ~ICommand() = default;

    virtual void Do() = 0;
    virtual void Undo() = 0;
    [[nodiscard]] virtual std::string_view Label() const = 0;

    /// Absorbs `next` (already applied) into this command, so a drag becomes one undo step. Default: no merging.
    virtual bool MergeWith(const ICommand& next)
    {
        (void)next;
        return false;
    }
};

/// Linear undo history. Executing a command drops everything that was undone; the oldest commands fall off past
/// the capacity. Tracks a clean point (the last save) for the dirty flag.
class UndoStack
{
public:
    explicit UndoStack(std::size_t capacity = 256);

    /// Applies the command and records it, merging into the previous one when both allow it and no Seal came
    /// between them.
    void Execute(std::unique_ptr<ICommand> command);

    bool Undo();
    bool Redo();
    [[nodiscard]] bool CanUndo() const noexcept { return m_next > 0; }
    [[nodiscard]] bool CanRedo() const noexcept { return m_next < m_commands.size(); }
    /// Labels of what Undo / Redo would revert or reapply (empty if nothing).
    [[nodiscard]] std::string_view UndoLabel() const noexcept;
    [[nodiscard]] std::string_view RedoLabel() const noexcept;

    /// Ends the current merge run (e.g. the mouse button was released after a drag).
    void Seal() noexcept { m_sealed = true; }

    void MarkClean() noexcept { m_clean = m_next; }
    [[nodiscard]] bool IsDirty() const noexcept { return m_clean != m_next; }

    void Clear() noexcept;
    [[nodiscard]] std::size_t Size() const noexcept { return m_commands.size(); }
    /// How many recorded commands are applied (Undo lowers it, Redo and Execute raise it).
    [[nodiscard]] std::size_t Applied() const noexcept { return m_next; }

private:
    static constexpr std::size_t kUnreachable = std::numeric_limits<std::size_t>::max();

    std::deque<std::unique_ptr<ICommand>> m_commands;
    std::size_t m_next = 0;  ///< Commands [0, m_next) are applied.
    std::size_t m_clean = 0; ///< m_next at the clean point, or kUnreachable once that state can't come back.
    std::size_t m_capacity;
    bool m_sealed = true;
};

} // namespace interi::editor
