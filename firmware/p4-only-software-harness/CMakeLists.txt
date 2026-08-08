cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../main-deck-p4/components/usb_host_manager"
    "${CMAKE_CURRENT_LIST_DIR}/../main-deck-p4/components/controller_usb_host"
    "${CMAKE_CURRENT_LIST_DIR}/../main-deck-p4/components/controller_usb_audio"
    "${CMAKE_CURRENT_LIST_DIR}/../main-deck-p4/components/controller_profile"
    "${CMAKE_CURRENT_LIST_DIR}/../main-deck-p4/components/controller_profile_runtime"
    "${CMAKE_CURRENT_LIST_DIR}/../main-deck-p4/components/controller_runtime"
    "${CMAKE_CURRENT_LIST_DIR}/../main-deck-p4/components/controller_led_runtime"
    "${CMAKE_CURRENT_LIST_DIR}/../main-deck-p4/components/control_link"
    "${CMAKE_CURRENT_LIST_DIR}/../common/control_state_reconciler"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(p4-only-software-harness)
