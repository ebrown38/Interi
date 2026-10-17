// SPDX-License-Identifier: MIT
#include <interi/editor_ui/StructurePanel.h>

#include "ImGuiTestContext.h"
#include <gtest/gtest.h>

using namespace interi::editor;

TEST(StructurePanel, Draws_IdleAndPlaying)
{
    interi::editor::testing::ImGuiTestContext context;
    StructureTool tool;
    StructureRequests requests;
    (void)context.Frame([&] { requests = DrawStructurePanel(tool, PlayStatus{}); });
    EXPECT_FALSE(requests.play || requests.stop || requests.saveReplay || requests.verifyReplay);
    const PlayStatus playing{.playing = true,
                             .watching = true,
                             .step = 10,
                             .replaySteps = 100,
                             .pieces = 3,
                             .intactBonds = 12,
                             .message = "replay matches"};
    (void)context.Frame([&] { requests = DrawStructurePanel(tool, playing); });
    EXPECT_FALSE(requests.stop);
    EXPECT_TRUE(tool.stressOverlay);
}
