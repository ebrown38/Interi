// SPDX-License-Identifier: MIT
#pragma once

// The one way the audio module includes miniaudio, so every translation unit sees the same configuration. Private:
// no public header includes miniaudio (it pulls in platform headers such as windows.h).
// Only the device and the decoders are used: mixing, spatialization and limiting are the engine's own.
#define MA_NO_ENGINE
#define MA_NO_NODE_GRAPH
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_GENERATION
#define MA_NO_ENCODING
#include <miniaudio.h>
