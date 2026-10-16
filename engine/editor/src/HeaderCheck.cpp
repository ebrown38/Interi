// SPDX-License-Identifier: MIT
// Compiles every public editor header in a library translation unit so header-only code is also built with the
// engine's warnings and checked by clang-tidy.
#include <interi/editor/AssetBrowser.h>
#include <interi/editor/Commands.h>
#include <interi/editor/EditorCamera.h>
#include <interi/editor/EditorDocument.h>
#include <interi/editor/EditorMath.h>
#include <interi/editor/EditorScene.h>
#include <interi/editor/Gizmo.h>
#include <interi/editor/Inspector.h>
#include <interi/editor/Picking.h>
#include <interi/editor/Reflection.h>
#include <interi/editor/UndoStack.h>
