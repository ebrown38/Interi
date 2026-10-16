# SPDX-License-Identifier: MIT
interi_add_benchmark(interi_render_benchmarks
    SOURCES RenderBenchmarks.cpp
    DEPS interi::render)

# On a real Vulkan device (skips without one).
interi_add_benchmark(interi_render_gpu_benchmarks
    SOURCES ParticleBenchmarks.cpp
    DEPS interi::render interi::rhi_vulkan)
