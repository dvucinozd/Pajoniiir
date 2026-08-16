cmake_minimum_required(VERSION 3.22)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/components"
    "${CMAKE_CURRENT_LIST_DIR}/../common/firmware_health"
    "${CMAKE_CURRENT_LIST_DIR}/../common/ota_manifest"
    "${CMAKE_CURRENT_LIST_DIR}/../common/control_state_reconciler"
    "${CMAKE_CURRENT_LIST_DIR}/../common/control_event_scheduler")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(control-board-s3)
