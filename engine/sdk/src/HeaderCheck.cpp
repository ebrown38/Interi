// SPDX-License-Identifier: MIT
// Includes every public header so each one is compiled (and linted) on its own.
#include <interi/sdk/ContributionRegistry.h>
#include <interi/sdk/Manifest.h>
#include <interi/sdk/ModuleGraph.h>
#include <interi/sdk/SemVer.h>
#include <interi/sdk/Toolchain.h>

// Instantiate the registry template so it is compiled and linted with the library.
template class interi::sdk::ContributionRegistry<int>;
