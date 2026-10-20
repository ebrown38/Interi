// SPDX-License-Identifier: MIT
// Compiles every public platform header in a library translation unit so header-only code is also built with the
// engine's warnings and checked by clang-tidy.
#include <interi/platform/AsyncFileReader.h>
#include <interi/platform/DynamicLibrary.h>
#include <interi/platform/FileSystem.h>
#include <interi/platform/InputConfig.h>
#include <interi/platform/InputEvents.h>
#include <interi/platform/InputMapper.h>
#include <interi/platform/PrivateFiles.h>
#include <interi/platform/Window.h>
