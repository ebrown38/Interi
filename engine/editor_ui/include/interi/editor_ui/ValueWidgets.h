// SPDX-License-Identifier: MIT
#pragma once

#include <interi/values/Value.h>

#include <optional>

namespace interi::editor
{

/// Draws an editing widget for `value` under `schema` (its type, enum options, range, step, unit, read-only flag) and
/// returns the edited value in the frame it changed. Text is returned once, when the field is left; drags every
/// frame (callers merge them into one undo step and seal on IsItemDeactivated). Lists draw one widget per item plus
/// add and remove buttons.
[[nodiscard]] std::optional<values::Value> ValueWidget(const char* label, const values::ValueSchema& schema,
                                                       const values::Value& value);

} // namespace interi::editor
