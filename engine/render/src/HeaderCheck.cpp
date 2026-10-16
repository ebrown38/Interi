// SPDX-License-Identifier: MIT
// Compiles every public render header in a library translation unit so header-only code is also built with the
// engine's warnings and checked by clang-tidy.
#include <interi/render/Camera.h>
#include <interi/render/Clusters.h>
#include <interi/render/Decals.h>
#include <interi/render/ForwardRenderer.h>
#include <interi/render/FrameGraph.h>
#include <interi/render/Mesh.h>
#include <interi/render/MeshPool.h>
#include <interi/render/Particles.h>
#include <interi/render/Shadows.h>
#include <interi/render/Texture.h>
