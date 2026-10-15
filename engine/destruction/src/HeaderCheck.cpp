// SPDX-License-Identifier: MIT
// Compiles every public destruction header in a library translation unit so header-only code is also built with
// the engine's warnings and checked by clang-tidy.
#include <interi/destruction/BondGraph.h>
#include <interi/destruction/ChunkHierarchy.h>
#include <interi/destruction/CollisionHulls.h>
#include <interi/destruction/Damage.h>
#include <interi/destruction/DestructibleAsset.h>
#include <interi/destruction/FractureMesh.h>
#include <interi/destruction/Material.h>
#include <interi/destruction/StressBudget.h>
#include <interi/destruction/StressSolver.h>
#include <interi/destruction/Voronoi.h>
