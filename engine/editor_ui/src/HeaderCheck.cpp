// SPDX-License-Identifier: MIT
// Compiles every public editor_ui header in a library translation unit so header-only code is also built with the
// engine's warnings and checked by clang-tidy.
#include <interi/editor_ui/ChainEditor.h>
#include <interi/editor_ui/DataPanels.h>
#include <interi/editor_ui/EditorExtensions.h>
#include <interi/editor_ui/EditorPanels.h>
#include <interi/editor_ui/HudDesigner.h>
#include <interi/editor_ui/ImGuiInput.h>
#include <interi/editor_ui/ImGuiRenderer.h>
#include <interi/editor_ui/ServicePanels.h>
#include <interi/editor_ui/ValueWidgets.h>
#include <interi/editor_ui/ViewportOverlay.h>
