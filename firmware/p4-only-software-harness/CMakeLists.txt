cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../p4-core/components/usb_host_manager"
    "${CMAKE_CURRENT_LIST_DIR}/../p4-core/components/controller_usb_host"
    "${CMAKE_CURRENT_LIST_DIR}/../p4-core/components/controller_usb_audio"
    "${CMAKE_CURRENT_LIST_DIR}/../p4-core/components/controller_profile"
    "${CMAKE_CURRENT_LIST_DIR}/../p4-core/components/controller_profile_runtime"
    "${CMAKE_CURRENT_LIST_DIR}/../p4-core/components/controller_runtime"
    "${CMAKE_CURRENT_LIST_DIR}/../p4-core/components/controller_led_runtime"
    "${CMAKE_CURRENT_LIST_DIR}/../p4-core/components/control_link"
    "${CMAKE_CURRENT_LIST_DIR}/../p4-core/components/board_capabilities"
    "${CMAKE_CURRENT_LIST_DIR}/../p4-core/components/service_log"
    "${CMAKE_CURRENT_LIST_DIR}/../p4-core/components/sd_io_gate"
    "${CMAKE_CURRENT_LIST_DIR}/../common/firmware_health"
    "${CMAKE_CURRENT_LIST_DIR}/../common/control_state_reconciler"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(p4-only-software-harness)

# Exercise the same pinned USB driver transforms as the production image.
# usb_host_manager requires the indexed idle-only recovery API added here.
include("${CMAKE_CURRENT_LIST_DIR}/../p4-core/cmake/apply_espressif_usb_fifo_patch.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../p4-core/cmake/apply_espressif_usb_idle_recovery_patch.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../p4-core/cmake/apply_usb_host_msc_teardown_patch.cmake")
