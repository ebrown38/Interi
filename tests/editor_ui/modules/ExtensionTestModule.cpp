// SPDX-License-Identifier: MIT
// A module library that extends the editor through the abstract API only (EditorExtensionsApi.h): no editor symbol
// is linked into it, which is what a real editor extension module looks like.
#include <interi/editor_ui/EditorExtensionsApi.h>
#include <interi/hotreload/ModuleApi.h>

namespace
{

using namespace interi::editor;

bool Register(interi::hotreload::ModuleRegistrar& registrar)
{
    auto* editor = interi::hotreload::ServiceAs<IEditorExtensions>(registrar, kEditorExtensionsService);
    if (editor == nullptr)
    {
        return true; // running in a game: nothing to extend
    }
    const interi::sdk::ModuleScope& scope = registrar.Scope();
    const bool ok =
        editor
            ->AddPanel(scope.Provider(), {.id = scope.Local("Stats"),
                                          .title = "Stats",
                                          .draw =
                                              [](const PanelContext& context, IEditorDocument& document)
                                          {
                                              // The editor's ImGui context arrives with the call.
                                              (void)document.ObjectName(context.imgui != nullptr ? 1 : 2);
                                          }})
            .has_value() &&
        editor
            ->AddOverlay(scope.Provider(), {.id = scope.Local("Marker"),
                                            .draw =
                                                [](IViewportCanvas& canvas, IEditorDocument& document)
                                            {
                                                const interi::Transform at = document.ObjectTransform(1);
                                                float x = 0.0f;
                                                float y = 0.0f;
                                                if (canvas.Project(at.position, x, y))
                                                {
                                                    canvas.Line(x, y, x + 4.0f, y + 4.0f, 0xff0000ffu, 1.0f);
                                                }
                                            }})
            .has_value() &&
        editor
            ->AddTool(scope.Provider(), {.id = scope.Local("Probe"),
                                         .name = "Probe",
                                         .activated = nullptr,
                                         .deactivated = nullptr,
                                         .frame =
                                             [](IViewportCanvas& canvas, const ToolInput& input, IEditorDocument&)
                                         {
                                             const interi::Ray ray = canvas.ScreenRay(input.mouseX, input.mouseY);
                                             canvas.WorldLine(ray.origin, ray.At(5.0f), 0xff00ff00u, 2.0f);
                                         }})
            .has_value() &&
        editor
            ->AddRenderFeature(scope.Provider(), {.id = scope.Local("Bounds"),
                                                  .contribute =
                                                      [](DebugDraw& draw, IEditorDocument& document)
                                                  {
                                                      for (const std::uint64_t id : document.SelectedIds())
                                                      {
                                                          draw.AddBox(document.ObjectTransform(id).position,
                                                                      interi::Vec3(0.5f), 0xffffffffu);
                                                      }
                                                  }})
            .has_value();
    return ok;
}

} // namespace

INTERI_GAMEPLAY_MODULE(Register)
