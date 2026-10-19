# SPDX-License-Identifier: MIT
# Performance benchmarks, one executable per module (interi_<module>_benchmarks). Hot-path code gets a benchmark
# alongside its tests (engine plan: Definition of Done).
add_subdirectory(core)
add_subdirectory(jobs)
add_subdirectory(ecs)
add_subdirectory(render)
add_subdirectory(audio)
