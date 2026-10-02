# Copyright (c) 2026 Hubble Network, Inc.
# SPDX-License-Identifier: Apache-2.0
#
# Builds app/ against the EM9305 SDK without modifying the SDK:
#
#   cmake -S . -B build -G Ninja   # or -G "Unix Makefiles"
#   cmake --build build --target app
#
# The EM9305 SDK's own top-level CMakeLists.txt sets up the toolchain, macros
# and library lists and then adds every example under projects/. This file does
# the same setup, pointed at the SDK through ROOT_DIR and adds the Hubble
# SDK and app/.

cmake_minimum_required(VERSION 3.19)

if(DEFINED ENV{EM9305_SDK_ROOT})
    file(TO_CMAKE_PATH "$ENV{EM9305_SDK_ROOT}" EM9305_SDK_DIR)
else()
    set(EM9305_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/EM9305_SDK")
endif()
set(HUBBLE_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/hubble-device-sdk")

if(NOT EXISTS "${EM9305_SDK_DIR}/cmake/toolchains/arcv2em.cmake")
    message(FATAL_ERROR
        "EM9305 SDK not found at ${EM9305_SDK_DIR}. Unpack it into "
        "external/EM9305_SDK or set EM9305_SDK_ROOT.")
endif()

# This setup mirrors SDK internals, so only the version it was written for
# is accepted.
set(_notes "${EM9305_SDK_DIR}/doc/sdk/ReleaseNotes.html")
if(EXISTS "${_notes}")
    file(READ "${_notes}" _notes_text)
    string(REGEX MATCH "EM9305[ \t\r\n]+([0-9]+\\.[0-9]+\\.[0-9]+)[ \t\r\n]+documentation"
        _match "${_notes_text}")
endif()
if(NOT CMAKE_MATCH_1 STREQUAL "4.6.1")
    message(FATAL_ERROR
        "EM9305 SDK 4.6.1 is required; found '${CMAKE_MATCH_1}' "
        "(read from ${_notes}).")
endif()

# Release adds -O3 -DNDEBUG to the SDK's -Os; Debug adds -g instead, which
# only a JTAG debugger needs.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Release or Debug" FORCE)
endif()

set(CMAKE_TOOLCHAIN_FILE "${EM9305_SDK_DIR}/cmake/toolchains/arcv2em.cmake")
project(hubble_reference C)
set(CMAKE_ASM_COMPILER_ID GNU)
enable_language(ASM)

# The include list alone is ~8.5 KB and cmd.exe truncates at 8191
# characters, cutting off the source file ("No files specified").
foreach(_kind INCLUDES OBJECTS LIBRARIES)
    set(CMAKE_C_USE_RESPONSE_FILE_FOR_${_kind} ON)
endforeach()

# The SDK links a MetaWare runtime matching the compiler.
execute_process(COMMAND ${CMAKE_C_COMPILER} --version OUTPUT_VARIABLE _mw)
string(REGEX MATCH "[A-Z]-([0-9]+\\.[0-9]+)" _match "${_mw}")
set(MW_VERSION "${CMAKE_MATCH_1}")
if(_mw MATCHES "EM-Micro")
    set(MW_FOR_EM TRUE)
elseif(NOT MW_VERSION STREQUAL "2019.09")
    message(WARNING "No MetaWare runtime for ${MW_VERSION} in the SDK; using 2019.09")
    set(MW_VERSION "2019.09")
endif()

# The rest of what the SDK's top level sets before its projects.
set(SDK_BUILD ON)
set(IMAGE_CRC_CHECK TRUE)
set(GENERATE_ASSEMBLY_DUMP ON)
set(ROOT_DIR "${EM9305_SDK_DIR}")
set(EMB_EXTENSIONS_DIR "${ROOT_DIR}/libs/third_party/emb/emb_extensions")

include(${ROOT_DIR}/cmake/t9305.cmake)
include(${ROOT_DIR}/cmake/t9305_design_iterations.cmake)
include(${ROOT_DIR}/emcore/bin/emcore_def.cmake OPTIONAL)

file(GLOB _emcore RELATIVE ${ROOT_DIR}/emcore/bin LIST_DIRECTORIES true
    ${ROOT_DIR}/emcore/bin/v*)
list(GET _emcore 0 EMCORE_VERSION)

# Only what app links is built.
add_subdirectory(${ROOT_DIR}/libs em9305_sdk/libs EXCLUDE_FROM_ALL)
add_subdirectory(${ROOT_DIR}/emcore em9305_sdk/emcore EXCLUDE_FROM_ALL)

add_subdirectory(app)
