// SPDX-License-Identifier: MIT
#pragma once

#include <interi/editor/EditorCamera.h>
#include <interi/editor/Gizmo.h>
#include <interi/editor_ui/EditorPanels.h>

struct ImDrawList;

namespace interi::editor
{

/// Draws a gizmo over the viewport image at `viewport` with the camera's view-projection, so it lines up with the
/// rendered scene. The hovered or dragged handle is highlighted.
void DrawGizmo(ImDrawList& list, const Gizmo& gizmo, const OrbitCamera& camera, const ScreenRect& viewport,
               GizmoAxis highlighted);

/// A world-space line over the viewport (clipped when an end is behind the eye).
void DrawWorldLine(ImDrawList& list, const OrbitCamera& camera, const ScreenRect& viewport, const Vec3& from,
                   const Vec3& to, unsigned int color, float thickness);

} // namespace interi::editor
