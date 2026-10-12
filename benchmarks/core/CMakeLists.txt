# SPDX-License-Identifier: MIT
interi_add_benchmark(interi_core_benchmarks
    SOURCES
        AllocatorBenchmarks.cpp
        ContainerBenchmarks.cpp
        MathBenchmarks.cpp
    DEPS interi::core)
