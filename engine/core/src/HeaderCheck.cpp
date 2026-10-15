// SPDX-License-Identifier: MIT
// Compiles every public core header in a library translation unit so header-only code is also built with the
// engine's warnings and checked by clang-tidy, and explicitly instantiates the container templates.
#include <interi/core/Archive.h>
#include <interi/core/Assert.h>
#include <interi/core/Config.h>
#include <interi/core/Hash.h>
#include <interi/core/Log.h>
#include <interi/core/Random.h>
#include <interi/core/Result.h>
#include <interi/core/StringId.h>
#include <interi/core/Version.h>
#include <interi/core/containers/FixedVector.h>
#include <interi/core/containers/HashMap.h>
#include <interi/core/containers/MPMCQueue.h>
#include <interi/core/containers/SPSCQueue.h>
#include <interi/core/containers/SlotMap.h>
#include <interi/core/containers/SmallVector.h>
#include <interi/core/math/Clipping.h>
#include <interi/core/math/ConvexHull.h>
#include <interi/core/math/Geometry.h>
#include <interi/core/math/Mat4.h>
#include <interi/core/math/MathKernels.h>
#include <interi/core/math/MathTypes.h>
#include <interi/core/math/Quat.h>
#include <interi/core/math/Transform.h>
#include <interi/core/math/Vec.h>
#include <interi/core/memory/Allocator.h>
#include <interi/core/memory/FrameAllocator.h>
#include <interi/core/memory/HeapAllocator.h>
#include <interi/core/memory/LinearAllocator.h>
#include <interi/core/memory/MemoryTracking.h>
#include <interi/core/memory/PoolAllocator.h>
#include <interi/core/memory/StlAllocator.h>
#include <interi/core/memory/TlsfAllocator.h>

#include <cstdint>
#include <string>

namespace interi
{
template class SlotMap<std::string>;
template class SmallVector<std::string, 4>;
template class FixedVector<std::string, 4>;
template class HashMap<std::string, std::uint32_t>;
template class MPMCQueue<std::string>;
template class SPSCQueue<std::string>;
} // namespace interi
