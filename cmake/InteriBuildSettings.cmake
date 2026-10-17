# SPDX-License-Identifier: MIT
# interi::build_settings: the engine's global ABI-relevant flags (CPU baseline, -ffp-contract=off, INTERI_SIMD_*,
# sanitizers; collected in INTERI_ABI_* by InteriArch.cmake and InteriSanitizers.cmake) and C++23, for code built
# out of tree against the installed engine. In-tree targets already get the flags directory-wide, so they apply only
# through the install interface. Every shared engine library links it publicly.

add_library(interi_build_settings INTERFACE)
add_library(interi::build_settings ALIAS interi_build_settings)
set_target_properties(interi_build_settings PROPERTIES EXPORT_NAME build_settings)
target_compile_features(interi_build_settings INTERFACE cxx_std_23)
foreach(_option IN LISTS INTERI_ABI_COMPILE_OPTIONS)
    target_compile_options(interi_build_settings INTERFACE "$<INSTALL_INTERFACE:${_option}>")
endforeach()
foreach(_definition IN LISTS INTERI_ABI_DEFINITIONS)
    target_compile_definitions(interi_build_settings INTERFACE "$<INSTALL_INTERFACE:${_definition}>")
endforeach()
foreach(_option IN LISTS INTERI_ABI_LINK_OPTIONS)
    target_link_options(interi_build_settings INTERFACE "$<INSTALL_INTERFACE:${_option}>")
endforeach()
