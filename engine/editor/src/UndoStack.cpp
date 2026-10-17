// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/editor/UndoStack.h>

namespace interi::editor
{

UndoStack::UndoStack(std::size_t capacity)
    : m_capacity(capacity)
{
    INTERI_ASSERT(capacity > 0, "UndoStack: capacity must be positive");
}

void UndoStack::Execute(std::unique_ptr<ICommand> command)
{
    command->Do();
    const bool redoDropped = m_next < m_commands.size();
    m_commands.erase(m_commands.begin() + static_cast<std::ptrdiff_t>(m_next), m_commands.end());
    if (m_clean != kUnreachable && m_clean > m_next)
    {
        m_clean = kUnreachable; // the clean state was in the dropped redo branch
    }
    if (!m_sealed && !redoDropped && m_next > 0 && m_commands[m_next - 1]->MergeWith(*command))
    {
        if (m_clean == m_next)
        {
            m_clean = kUnreachable; // the saved command now holds a different state
        }
        return;
    }
    m_commands.push_back(std::move(command));
    ++m_next;
    if (m_commands.size() > m_capacity)
    {
        m_commands.pop_front();
        --m_next;
        m_clean = (m_clean == kUnreachable || m_clean == 0) ? kUnreachable : m_clean - 1;
    }
    m_sealed = false;
}

bool UndoStack::Undo()
{
    if (!CanUndo())
    {
        return false;
    }
    m_commands[--m_next]->Undo();
    m_sealed = true;
    return true;
}

bool UndoStack::Redo()
{
    if (!CanRedo())
    {
        return false;
    }
    m_commands[m_next++]->Do();
    m_sealed = true;
    return true;
}

std::string_view UndoStack::UndoLabel() const noexcept
{
    return CanUndo() ? m_commands[m_next - 1]->Label() : std::string_view();
}

std::string_view UndoStack::RedoLabel() const noexcept
{
    return CanRedo() ? m_commands[m_next]->Label() : std::string_view();
}

void UndoStack::Clear() noexcept
{
    m_commands.clear();
    m_next = 0;
    m_clean = 0;
    m_sealed = true;
}

} // namespace interi::editor
