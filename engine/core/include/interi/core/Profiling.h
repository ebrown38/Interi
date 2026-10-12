// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Config.h>

/// CPU profiling hooks, backed by Tracy (https://github.com/wolfpld/tracy) when the build enables them.
///
/// INTERI_PROFILING_ACTIVE is 1 when CMake's INTERI_PROFILING option is ON (the *-profile presets) and the build
/// is not INTERI_SHIPPING; otherwise every macro below expands to nothing and Tracy is not linked. Tracy is built
/// in on-demand mode: zones cost a few nanoseconds and record nothing until a Tracy profiler connects. Tracy
/// calibrates its timer when the process starts, which adds roughly half a second to start-up in profile builds.
///
///     void Fracture(...)
///     {
///         INTERI_ZONE();                      // named after the function
///         INTERI_ZONE_NAMED("Solve stress");  // explicit name
///     }
///     INTERI_FRAME_MARK();                    // once per frame, on the main thread
#if defined(INTERI_ENABLE_PROFILING) && INTERI_ENABLE_PROFILING && !defined(INTERI_SHIPPING)
#define INTERI_PROFILING_ACTIVE 1
#else
#define INTERI_PROFILING_ACTIVE 0
#endif

#if INTERI_PROFILING_ACTIVE
#include <tracy/Tracy.hpp>

#define INTERI_ZONE() ZoneScoped
#define INTERI_ZONE_NAMED(name) ZoneScopedN(name)
/// Attaches run-time text (e.g. an asset name) to the current zone.
#define INTERI_ZONE_TEXT(text, size) ZoneText(text, size)
#define INTERI_FRAME_MARK() FrameMark
/// Plots a numeric value over time (e.g. active debris count).
#define INTERI_PROFILE_PLOT(name, value) TracyPlot(name, value)
#define INTERI_PROFILE_THREAD_NAME(name) ::tracy::SetThreadName(name)
#else
#define INTERI_ZONE() static_cast<void>(0)
#define INTERI_ZONE_NAMED(name) static_cast<void>(0)
#define INTERI_ZONE_TEXT(text, size) static_cast<void>(0)
#define INTERI_FRAME_MARK() static_cast<void>(0)
#define INTERI_PROFILE_PLOT(name, value) static_cast<void>(0)
#define INTERI_PROFILE_THREAD_NAME(name) static_cast<void>(0)
#endif
