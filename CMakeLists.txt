# Organya Playback Plugin
#
# This plugin plays Organya music files (.org) from the Cave Story engine.
# Uses Strultz's organya.h single-header library.

cmake_minimum_required(VERSION 3.16)
project(playback_organya C)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(PlaybackPlugin)

# Download organya.h
download_library(
    NAME organya
    URL "https://github.com/Strultz/organya.h/archive/59fe23704e53f5acff7a319964215f41d61ee652.tar.gz"
    FILENAME "organya.h-59fe237.tar.gz"
    HASH "380dc2ec38d5b52e540e9f9479e0b33f74252f8f6e62ff384a0ebc269b786da0"
    EXTRACT_DIR "organya.h-59fe23704e53f5acff7a319964215f41d61ee652"
    PATCHES patches/scope-capture.patch
)

add_library(organya_playback SHARED
    organya_plugin.c
    organya_impl.c
)

target_include_directories(organya_playback PRIVATE
    ${organya_SOURCE_DIR}
)

target_compile_definitions(organya_playback PRIVATE
    ORG_USE_STDINT
    ORG_NO_STDIO
)

if(UNIX)
    target_link_libraries(organya_playback PRIVATE m)
endif()

suppress_external_warnings(organya_playback)

rv_set_plugin_properties(organya_playback)
