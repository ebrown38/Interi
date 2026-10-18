// SPDX-License-Identifier: MIT
#pragma once

// The editor's component reflection is the values module's (interi/values/Reflection.h); these names keep the
// editor's spelling.

#include <interi/values/Reflection.h>
#include <interi/values/Value.h>

namespace interi::editor
{

/// The field types the inspector can show and the undo stack can record.
using FieldType = values::ValueType;
/// One field's value; the alternative index matches FieldType.
using FieldValue = values::Value;

using values::Field;
using values::FieldInfo;
using values::ReadField;
using values::ReflectionRegistry;
using values::TypeOf;
using values::TypeReflection;
using values::WriteField;

/// Registers what scene documents hold: the scene components (name, parent, local transform; the world Transform is
/// derived and not reflected), primitives and meshes, the level bridge's components (LevelBridge.h) and the gameplay
/// components with kComponentNames (VehicleComponent, WreckComponent).
void RegisterEngineReflection(ReflectionRegistry& registry);

} // namespace interi::editor
