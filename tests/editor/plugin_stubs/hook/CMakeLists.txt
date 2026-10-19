# SPDX-License-Identifier: MIT
# Hook Plugin (com.interi.test.hookplugin): an Interi Plugin, built against an installed engine.
cmake_minimum_required(VERSION 3.29)
project(com_interi_test_hookplugin LANGUAGES CXX)

find_package(Interi REQUIRED)

interi_add_gameplay_module(com_interi_test_hookplugin SOURCES src/HookPluginModule.cpp)

# ctest: loads this folder's module.json and the library through the engine's module host.
enable_testing()
interi_add_module_load_test(com_interi_test_hookplugin)
