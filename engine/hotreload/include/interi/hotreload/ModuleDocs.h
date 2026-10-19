// SPDX-License-Identifier: MIT
#pragma once

// Module documentation from a manifest: a Markdown page listing a module's metadata, Events, Functions, hook points,
// Values and Data Tables. Values and tables come from the manifest's data sections; Events, Functions and hooks are
// registered by the module's code, so they are listed when the module is loaded (a host's registries are passed in).

#include <interi/chain/Event.h>
#include <interi/core/Result.h>
#include <interi/sdk/Hooks.h>
#include <interi/sdk/Manifest.h>

#include <string>

namespace interi::hotreload
{

/// The page for `manifest`. `chains` and `hooks` (either may be nullptr) supply what the module's code provides: the
/// items `manifest.id` contributes, including overrides of other modules' items. The data sections are read from
/// `<manifest.directory>/module.json` (IoError / InvalidArgument when they can't be). Deterministic: sorted by id.
[[nodiscard]] Result<std::string> GenerateModuleDocs(const sdk::ModuleManifest& manifest,
                                                     const chain::ChainRegistry* chains,
                                                     const sdk::HookRegistry* hooks);

} // namespace interi::hotreload
