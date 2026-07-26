cmake_minimum_required(VERSION 3.20)
project(pajoniiir_ui_simulator_e2e C CXX)

if(NOT LVGL_DIR)
    message(FATAL_ERROR "LVGL_DIR must point to the pinned LVGL checkout")
endif()

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE FILEPATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
set(LV_FETCH_DEPENDENCIES OFF CACHE BOOL "" FORCE)

add_subdirectory("${LVGL_DIR}" "${CMAKE_BINARY_DIR}/lvgl" EXCLUDE_FROM_ALL)

set(UI_DIR "${REPO_ROOT}/firmware/main-deck-p4/components/ui")
set(LIBRARY_DIR "${REPO_ROOT}/firmware/main-deck-p4/components/library")

add_executable(ui_simulator_e2e
    ui_simulator_e2e.c
    simulator_library.c
    simulator_mocks.c

    "${UI_DIR}/ui.c"
    "${UI_DIR}/splash_screen.c"
    "${UI_DIR}/Musieer_80.c"
    "${UI_DIR}/Musieer_48.c"
    "${UI_DIR}/ui_library.c"
    "${UI_DIR}/ui_overview.c"
    "${UI_DIR}/ui_active_deck_leds.c"
    "${UI_DIR}/ui_controls.c"
    "${UI_DIR}/ui_deck_anlz_store.c"
    "${UI_DIR}/ui_mixer_view.c"
    "${UI_DIR}/ui_overview_motion.c"
    "${UI_DIR}/ui_overlay_map.c"
    "${UI_DIR}/ui_overview_perf.c"
    "${UI_DIR}/ui_overview_scheduler.c"
    "${UI_DIR}/ui_overview_renderer.c"
    "${UI_DIR}/ui_overview_wave_cache.c"
    "${UI_DIR}/ui_overview_grid.c"
    "${UI_DIR}/ui_performance_target.c"
    "${UI_DIR}/ui_performance_tabs.c"
    "${UI_DIR}/ui_position_interpolator.c"
    "${UI_DIR}/ui_waveform_model.c"
    "${UI_DIR}/ui_beat_indicator.c"
    "${UI_DIR}/ui_settings.c"
    "${UI_DIR}/ui_status.c"
    "${UI_DIR}/ui_beat_fx_format.c"
    "${UI_DIR}/ui_overview_window.c"

    "${LIBRARY_DIR}/rekordbox_anlz.c"
    "${REPO_ROOT}/firmware/main-deck-p4/components/beat_jump/beat_jump.c"
    "${REPO_ROOT}/firmware/main-deck-p4/components/control_link/flx4_led_snapshot.c"
    "${REPO_ROOT}/firmware/main-deck-p4/components/deck_core/deck_core.c"
    "${REPO_ROOT}/tests/deck_core_dual/control_link_stub.c"
    "${REPO_ROOT}/tests/deck_core_dual/hot_cue_store_stub.c"
)

target_compile_definitions(ui_simulator_e2e PRIVATE
    WIN32=1
    DECK_CORE_PC_TEST=1
    REKORDBOX_PDB_STANDALONE_TEST=1
    CONFIG_AUDIO_RECORDER_ENABLED=0
    CONFIG_AUDIO_SCRATCH_ENABLED=0
)

target_include_directories(ui_simulator_e2e PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${REPO_ROOT}/tests/deck_core_dual/stubs"
    "${UI_DIR}/include"
    "${LIBRARY_DIR}/include"
    "${REPO_ROOT}/firmware/main-deck-p4/components/deck_core/include"
    "${REPO_ROOT}/firmware/main-deck-p4/components/beat_jump/include"
    "${REPO_ROOT}/firmware/main-deck-p4/components/control_link/include"
    "${REPO_ROOT}/firmware/main-deck-p4/components/hot_cue_store/include"
    "${REPO_ROOT}/firmware/main-deck-p4/components/audio_engine/include"
    "${REPO_ROOT}/firmware/main-deck-p4/components/audio_mixer/include"
    "${REPO_ROOT}/firmware/main-deck-p4/components/app_settings/include"
    "${REPO_ROOT}/firmware/main-deck-p4/components/media_io_gate/include"
)

target_link_libraries(ui_simulator_e2e PRIVATE lvgl m)

if(MINGW)
    target_link_options(ui_simulator_e2e PRIVATE -static-libgcc -static-libstdc++)
endif()
