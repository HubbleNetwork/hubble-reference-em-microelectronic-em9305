# Copyright (c) 2026 Hubble Network, Inc.
# SPDX-License-Identifier: Apache-2.0
#
# The beacon image, built with the EM9305 SDK's macros. Configure from the
# repository root, which sets the SDK up; this file cannot be built alone.

PROJECT(app C)

IF(NOT EXISTS "${HUBBLE_SDK_DIR}/src/hubble.c")
    MESSAGE(FATAL_ERROR
        "Hubble Device SDK missing at ${HUBBLE_SDK_DIR}. "
        "Run: git submodule update --init")
ENDIF()

SET(${PROJECT_NAME}_SRCS
    nvm_main.c
    ble/source/beacon.c
    hubble/source/hubble_port.c
    hubble/source/hubble_crypto_em.c
    ${HUBBLE_SDK_DIR}/src/hubble.c
    ${HUBBLE_SDK_DIR}/src/hubble_ble.c
    ${HUBBLE_SDK_DIR}/src/hubble_crypto.c
)

SET(${PROJECT_NAME}_LIBS
    ${NVM_COMMON_LIBS}
    QPC
    emb_peripheral_legacy
    rtc
    printf
    terminal_handler
)

SET(${PROJECT_NAME}_INCLUDES
    ${EMB_COMMON_INCLUDES}
    ${LIBS_DIR}/terminal_handler/includes
    ble/includes
    hubble/includes
    ${HUBBLE_SDK_DIR}/include
    ${HUBBLE_SDK_DIR}/src
)

SET(${PROJECT_NAME}_FLAGS
    "-imacros ${CMAKE_CURRENT_SOURCE_DIR}/hubble/includes/hubble_config.h")

APP_IN_NVM()
ARC_EXECUTABLE(${PROJECT_NAME} ${${PROJECT_NAME}_SRCS})
ARC_LINK_LIBRARIES(${PROJECT_NAME} ${${PROJECT_NAME}_LIBS})
ARC_INCLUDE_DIRECTORIES(${PROJECT_NAME} ${${PROJECT_NAME}_INCLUDES})
ADD_TARGET_COMPILE_FLAGS_ALL_BUILD_CONFIGS(${PROJECT_NAME}
    ${${PROJECT_NAME}_FLAGS})
