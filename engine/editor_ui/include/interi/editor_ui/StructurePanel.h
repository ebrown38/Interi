// SPDX-License-Identifier: MIT
#pragma once

#include <interi/editor_ui/EditorPanels.h>

#include <cstdint>
#include <string>

namespace interi::editor
{

/// Settings of the structural debugging tools.
struct StructureTool
{
    bool stressOverlay = true; ///< Bonds as lines coloured by utilization (StressOverlayColor).
    bool anchors = true;       ///< Anchor chunks as markers.
    bool detonate = false;     ///< Left mouse in the viewport sets off an explosion where it points.
    float energy = 5000.0f;    ///< J per bond within the inner radius.
    float innerRadius = 1.0f;
    float radius = 3.0f;
    bool paused = false;
    std::string replayPath; ///< Where Save / Verify / Watch read or write a replay.
};

/// What the play session looks like now (filled in by the app).
struct PlayStatus
{
    bool playing = false;
    bool watching = false; ///< Playing back a replay.
    std::uint64_t step = 0;
    std::uint64_t replaySteps = 0;
    std::size_t pieces = 0;
    std::size_t intactBonds = 0;
    std::string message; ///< Last replay result.
};

struct StructureRequests
{
    bool play = false;
    bool stop = false;
    bool step = false; ///< One step while paused.
    bool saveReplay = false;
    bool verifyReplay = false;
    bool watchReplay = false;
};

/// Play-in-editor controls (play, stop, pause, step), the stress heatmap and anchor toggles, "detonate here" with
/// its blast, and replays: save the session, verify a file (replayed headless, final hash compared) or watch it.
[[nodiscard]] StructureRequests DrawStructurePanel(StructureTool& tool, const PlayStatus& status);

} // namespace interi::editor
