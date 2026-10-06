# Pure board descriptions are also used by the USB-only software harness.
# Keep one source/header; no display/codec/SD physical dependencies here.
idf_component_register(SRCS "board_capabilities.c"
    INCLUDE_DIRS "include" REQUIRES esp_common)
