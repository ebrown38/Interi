// SPDX-License-Identifier: MIT
#include <interi/editor/UndoStack.h>

#include <gtest/gtest.h>

#include <memory>
#include <string>

using namespace interi::editor;

namespace
{

/// Sets *target to a value; merges with later commands of the same key.
class SetInt final : public ICommand
{
public:
    SetInt(int& target, int value, int key = 0)
        : m_target(&target)
        , m_before(target)
        , m_after(value)
        , m_key(key)
    {
    }

    void Do() override { *m_target = m_after; }
    void Undo() override { *m_target = m_before; }
    [[nodiscard]] std::string_view Label() const override { return "Set"; }
    bool MergeWith(const ICommand& next) override
    {
        const auto* other = dynamic_cast<const SetInt*>(&next);
        if (m_key == 0 || other == nullptr || other->m_key != m_key || other->m_target != m_target)
        {
            return false;
        }
        m_after = other->m_after;
        return true;
    }

private:
    int* m_target;
    int m_before;
    int m_after;
    int m_key;
};

} // namespace

TEST(UndoStack, DoUndoRedo_StateRestored)
{
    int state = 0;
    UndoStack undo;
    undo.Execute(std::make_unique<SetInt>(state, 1));
    undo.Execute(std::make_unique<SetInt>(state, 2));
    EXPECT_EQ(state, 2);
    EXPECT_EQ(undo.UndoLabel(), "Set");

    EXPECT_TRUE(undo.Undo());
    EXPECT_EQ(state, 1);
    EXPECT_TRUE(undo.Undo());
    EXPECT_EQ(state, 0);
    EXPECT_FALSE(undo.Undo());
    EXPECT_FALSE(undo.CanUndo());

    EXPECT_TRUE(undo.Redo());
    EXPECT_EQ(state, 1);
    EXPECT_TRUE(undo.Redo());
    EXPECT_EQ(state, 2);
    EXPECT_FALSE(undo.Redo());
}

TEST(UndoStack, ExecuteAfterUndo_DropsRedo)
{
    int state = 0;
    UndoStack undo;
    undo.Execute(std::make_unique<SetInt>(state, 1));
    undo.Execute(std::make_unique<SetInt>(state, 2));
    ASSERT_TRUE(undo.Undo());
    undo.Execute(std::make_unique<SetInt>(state, 5));
    EXPECT_FALSE(undo.CanRedo());
    EXPECT_EQ(undo.Size(), 2u);
    ASSERT_TRUE(undo.Undo());
    EXPECT_EQ(state, 1);
}

TEST(UndoStack, SameMergeKey_OneStepUntilSealed)
{
    int state = 0;
    UndoStack undo;
    for (int value = 1; value <= 5; ++value)
    {
        undo.Execute(std::make_unique<SetInt>(state, value, 7));
    }
    EXPECT_EQ(undo.Size(), 1u);
    undo.Seal();
    undo.Execute(std::make_unique<SetInt>(state, 9, 7));
    EXPECT_EQ(undo.Size(), 2u);

    ASSERT_TRUE(undo.Undo());
    EXPECT_EQ(state, 5);
    ASSERT_TRUE(undo.Undo());
    EXPECT_EQ(state, 0);
}

TEST(UndoStack, OverCapacity_DropsOldest)
{
    int state = 0;
    UndoStack undo(3);
    for (int value = 1; value <= 5; ++value)
    {
        undo.Execute(std::make_unique<SetInt>(state, value));
    }
    EXPECT_EQ(undo.Size(), 3u);
    while (undo.Undo())
    {
    }
    EXPECT_EQ(state, 2); // 1 -> 2 fell off
}

TEST(UndoStack, CleanPoint_TracksDirty)
{
    int state = 0;
    UndoStack undo;
    EXPECT_FALSE(undo.IsDirty());
    undo.Execute(std::make_unique<SetInt>(state, 1));
    EXPECT_TRUE(undo.IsDirty());
    undo.MarkClean();
    EXPECT_FALSE(undo.IsDirty());
    ASSERT_TRUE(undo.Undo());
    EXPECT_TRUE(undo.IsDirty());
    ASSERT_TRUE(undo.Redo());
    EXPECT_FALSE(undo.IsDirty());

    // Undo past the clean point and branch off: the clean state can't come back.
    ASSERT_TRUE(undo.Undo());
    undo.Execute(std::make_unique<SetInt>(state, 3));
    ASSERT_TRUE(undo.Undo());
    EXPECT_TRUE(undo.IsDirty());
}

TEST(UndoStack, MergeIntoCleanPoint_Dirty)
{
    int state = 0;
    UndoStack undo;
    undo.Execute(std::make_unique<SetInt>(state, 1, 4));
    undo.MarkClean();
    undo.Execute(std::make_unique<SetInt>(state, 2, 4)); // merges into the saved command
    EXPECT_TRUE(undo.IsDirty());
}
