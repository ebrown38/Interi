# SPDX-License-Identifier: MIT
interi_add_benchmark(interi_destruction_runtime_benchmarks
    SOURCES CollapseBenchmarks.cpp
    DEPS interi::destruction_runtime interi::physics_testing interi::testing)
