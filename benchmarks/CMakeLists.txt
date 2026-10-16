# SPDX-License-Identifier: MIT
# Performance benchmarks, one executable per module (interi_<module>_benchmarks). Hot-path code gets a benchmark
# alongside its tests (engine plan: Definition of Done).
add_subdirectory(core)
add_subdirectory(jobs)
add_subdirectory(ecs)
add_subdirectory(physics)
add_subdirectory(destruction)
add_subdirectory(destruction_runtime)
add_subdirectory(destruction_render)
add_subdirectory(render)
