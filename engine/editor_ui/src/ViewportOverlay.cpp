// SPDX-License-Identifier: MIT
#include <interi/editor_ui/ViewportOverlay.h>

#include <imgui.h>

#include <cmath>
#include <numbers>

namespace interi::editor
{

namespace
{

[[nodiscard]] ImU32 AxisColor(GizmoAxis axis, bool highlighted) noexcept
{
    if (highlighted)
    {
        return IM_COL32(255, 220, 40, 255);
    }
    switch (axis)
    {
    case GizmoAxis::X:
        return IM_COL32(230, 60, 60, 255);
    case GizmoAxis::Y:
        return IM_COL32(80, 210, 80, 255);
    case GizmoAxis::Z:
        return IM_COL32(70, 120, 240, 255);
    case GizmoAxis::None:
        break;
    }
    return IM_COL32_WHITE;
}

} // namespace

void DrawWorldLine(ImDrawList& list, const OrbitCamera& camera, const ScreenRect& viewport, const Vec3& from,
                   const Vec3& to, unsigned int color, float thickness)
{
    const auto a = camera.Project(from, viewport.width, viewport.height);
    const auto b = camera.Project(to, viewport.width, viewport.height);
    if (!a || !b)
    {
        return;
    }
    list.AddLine(ImVec2(viewport.x + a->x, viewport.y + a->y), ImVec2(viewport.x + b->x, viewport.y + b->y), color,
                 thickness);
}

void DrawGizmo(ImDrawList& list, const Gizmo& gizmo, const OrbitCamera& camera, const ScreenRect& viewport,
               GizmoAxis highlighted)
{
    list.PushClipRect(ImVec2(viewport.x, viewport.y), ImVec2(viewport.x + viewport.width, viewport.y + viewport.height),
                      true);
    for (const GizmoAxis axis : {GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z})
    {
        const ImU32 color = AxisColor(axis, axis == highlighted);
        const Vec3 direction = AxisVector(axis);
        if (gizmo.mode == GizmoMode::Rotate)
        {
            const Vec3 helper = std::abs(direction.y) < 0.9f ? Vec3(0.0f, 1.0f, 0.0f) : Vec3(1.0f, 0.0f, 0.0f);
            const Vec3 tangent = Normalize(Cross(helper, direction));
            const Vec3 bitangent = Cross(direction, tangent);
            constexpr int kSegments = 48;
            Vec3 previous = gizmo.origin + tangent * gizmo.size;
            for (int i = 1; i <= kSegments; ++i)
            {
                const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / kSegments;
                const Vec3 point =
                    gizmo.origin + (tangent * std::cos(angle) + bitangent * std::sin(angle)) * gizmo.size;
                DrawWorldLine(list, camera, viewport, previous, point, color, 2.5f);
                previous = point;
            }
            continue;
        }
        const Vec3 tip = gizmo.origin + direction * gizmo.size;
        DrawWorldLine(list, camera, viewport, gizmo.origin, tip, color, 3.0f);
        if (const auto end = camera.Project(tip, viewport.width, viewport.height))
        {
            const ImVec2 at(viewport.x + end->x, viewport.y + end->y);
            if (gizmo.mode == GizmoMode::Scale)
            {
                list.AddRectFilled(ImVec2(at.x - 5.0f, at.y - 5.0f), ImVec2(at.x + 5.0f, at.y + 5.0f), color);
            }
            else
            {
                list.AddCircleFilled(at, 6.0f, color);
            }
        }
    }
    list.PopClipRect();
}

} // namespace interi::editor
