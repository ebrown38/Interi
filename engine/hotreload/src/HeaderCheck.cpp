// SPDX-License-Identifier: MIT
// Compiles every public hotreload header in a library translation unit so header-only code is also built with the
// engine's warnings and checked by clang-tidy.
#include <interi/hotreload/FileWatcher.h>
#include <interi/hotreload/ModuleApi.h>
#include <interi/hotreload/ModuleHost.h>
