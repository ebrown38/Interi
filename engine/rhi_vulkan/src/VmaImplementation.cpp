// SPDX-License-Identifier: MIT
// The Vulkan Memory Allocator implementation, compiled once. Third-party code: the engine's warnings are disabled
// for this file only (it is also excluded from clang-tidy).
#include <volk.h>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
