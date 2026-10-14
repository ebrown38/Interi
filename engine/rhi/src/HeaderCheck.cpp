// SPDX-License-Identifier: MIT
// Compiles every public rhi header in a library translation unit so header-only code is also built with the
// engine's warnings and checked by clang-tidy.
#include <interi/rhi/CommandListPool.h>
#include <interi/rhi/Device.h>
#include <interi/rhi/ResourceManager.h>
#include <interi/rhi/Swapchain.h>
#include <interi/rhi/Types.h>
#include <interi/rhi/UploadQueue.h>
