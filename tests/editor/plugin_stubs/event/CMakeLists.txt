# SPDX-License-Identifier: MIT
# Event Plugin (com.interi.test.eventplugin): an Interi Plugin, built against an installed engine.
cmake_minimum_required(VERSION 3.29)
project(com_interi_test_eventplugin LANGUAGES CXX)

find_package(Interi REQUIRED)

interi_add_gameplay_module(com_interi_test_eventplugin SOURCES src/EventPluginModule.cpp)

# ctest: loads this folder's module.json and the library through the engine's module host.
enable_testing()
interi_add_module_load_test(com_interi_test_eventplugin)
