# The P4-local image uses the ESP32-P4 HS DWC controller for USB mass storage
# and the FS DWC controller for controller MIDI plus FLX4 UAC1 output.  The
# pinned esp-usb driver exposes one global FIFO bias, which cannot satisfy both
# workloads.  Keep this narrow, fail-closed source transformation until the
# dependency supports per-controller FIFO configuration.  The generated copy
# remains under build/, leaving managed_components pristine.

# The pinned git dependency registers its IDF component target as "usb".
# Component-manager directory names are not stable enough to use as a target.
idf_component_get_property(_pajoniiir_usb_dir usb COMPONENT_DIR)
set(_pajoniiir_hcd_source "${_pajoniiir_usb_dir}/src/hcd_dwc.c")

if(NOT EXISTS "${_pajoniiir_hcd_source}")
    message(FATAL_ERROR
        "Pinned esp-usb HCD source was not found: ${_pajoniiir_hcd_source}")
endif()

set(_pajoniiir_fifo_upstream [=[
    const int otg_dfifo_depth = hal->constant_config.hsphy_type ? 1024 : 256;
    const uint16_t fifo_size_lines = hal->constant_config.fifo_size;

#if CONFIG_USB_HOST_HW_BUFFER_BIAS_IN
    // Prioritize RX FIFO (best for IN-heavy workloads)
    port->fifo_config.nptx_fifo_lines = otg_dfifo_depth / 16;
    port->fifo_config.ptx_fifo_lines = otg_dfifo_depth / 8;
    port->fifo_config.rx_fifo_lines = fifo_size_lines - port->fifo_config.ptx_fifo_lines - port->fifo_config.nptx_fifo_lines;

#elif CONFIG_USB_HOST_HW_BUFFER_BIAS_PERIODIC_OUT
    // Prioritize periodic TX FIFO (useful for high throughput periodic endpoints)
    port->fifo_config.rx_fifo_lines = otg_dfifo_depth / 8 + 2; // 2 extra lines are allocated for status information. See USB-OTG Programming Guide, chapter 2.1.2.1
    port->fifo_config.nptx_fifo_lines = otg_dfifo_depth / 16;
    port->fifo_config.ptx_fifo_lines = fifo_size_lines - port->fifo_config.nptx_fifo_lines - port->fifo_config.rx_fifo_lines;

#else // USB_HOST_HW_BUFFER_BIAS_BALANCED
    // Balanced configuration (default)
    port->fifo_config.nptx_fifo_lines = otg_dfifo_depth / 4;
    port->fifo_config.ptx_fifo_lines = otg_dfifo_depth / 8;
    port->fifo_config.rx_fifo_lines = fifo_size_lines - port->fifo_config.ptx_fifo_lines - port->fifo_config.nptx_fifo_lines;
#endif
]=])

set(_pajoniiir_fifo_patched [=[
    const bool is_hs = (hal->constant_config.hsphy_type != 0);
    const uint16_t fifo_size_lines = hal->constant_config.fifo_size;

    if (is_hs) {
        // HS controller: reserve RX and non-periodic TX space for 512-byte
        // mass-storage bulk packets.
        port->fifo_config.nptx_fifo_lines = 256;
        port->fifo_config.ptx_fifo_lines = 128;
        port->fifo_config.rx_fifo_lines = fifo_size_lines
                                          - port->fifo_config.ptx_fifo_lines
                                          - port->fifo_config.nptx_fifo_lines;
    } else {
        // FS controller: MIDI uses 64-byte bulk packets and FLX4 UAC1 uses
        // periodic OUT packets up to 384 bytes.
        port->fifo_config.nptx_fifo_lines = 20;
        port->fifo_config.ptx_fifo_lines = 100;
        port->fifo_config.rx_fifo_lines = fifo_size_lines
                                          - port->fifo_config.ptx_fifo_lines
                                          - port->fifo_config.nptx_fifo_lines;
    }
]=])

file(READ "${_pajoniiir_hcd_source}" _pajoniiir_hcd_contents)
string(FIND "${_pajoniiir_hcd_contents}" "${_pajoniiir_fifo_upstream}"
       _pajoniiir_upstream_at)
if(_pajoniiir_upstream_at EQUAL -1)
    message(FATAL_ERROR
        "esp-usb hcd_dwc.c no longer matches the pinned FIFO block; review "
        "the Pajoniiir HS/FS FIFO patch before building")
endif()

string(REPLACE "${_pajoniiir_fifo_upstream}" "${_pajoniiir_fifo_patched}"
       _pajoniiir_hcd_contents "${_pajoniiir_hcd_contents}")

set(_pajoniiir_generated_dir "${CMAKE_BINARY_DIR}/pajoniiir_usb")
set(_pajoniiir_generated_hcd "${_pajoniiir_generated_dir}/hcd_dwc.c")
file(MAKE_DIRECTORY "${_pajoniiir_generated_dir}")
file(WRITE "${_pajoniiir_generated_hcd}" "${_pajoniiir_hcd_contents}")
set_source_files_properties("${_pajoniiir_generated_hcd}" PROPERTIES GENERATED TRUE)

idf_component_get_property(_pajoniiir_usb_lib usb COMPONENT_LIB)
get_target_property(_pajoniiir_usb_sources "${_pajoniiir_usb_lib}" SOURCES)
set(_pajoniiir_usb_replacement_sources)
set(_pajoniiir_hcd_source_found FALSE)
foreach(_pajoniiir_usb_source IN LISTS _pajoniiir_usb_sources)
    get_filename_component(_pajoniiir_usb_source_abs "${_pajoniiir_usb_source}"
                           ABSOLUTE BASE_DIR "${_pajoniiir_usb_dir}")
    if(_pajoniiir_usb_source_abs STREQUAL _pajoniiir_hcd_source)
        list(APPEND _pajoniiir_usb_replacement_sources "${_pajoniiir_generated_hcd}")
        set(_pajoniiir_hcd_source_found TRUE)
    else()
        list(APPEND _pajoniiir_usb_replacement_sources "${_pajoniiir_usb_source}")
    endif()
endforeach()

if(NOT _pajoniiir_hcd_source_found)
    message(FATAL_ERROR
        "Could not replace esp-usb hcd_dwc.c in the component source list")
endif()

set_property(TARGET "${_pajoniiir_usb_lib}" PROPERTY SOURCES
             "${_pajoniiir_usb_replacement_sources}")
message(STATUS "Using generated Pajoniiir USB HS/FS FIFO-patched HCD source")

unset(_pajoniiir_hcd_contents)
unset(_pajoniiir_hcd_source)
unset(_pajoniiir_usb_dir)
unset(_pajoniiir_usb_lib)
unset(_pajoniiir_usb_sources)
unset(_pajoniiir_usb_replacement_sources)
unset(_pajoniiir_usb_source)
unset(_pajoniiir_usb_source_abs)
unset(_pajoniiir_generated_dir)
unset(_pajoniiir_generated_hcd)
unset(_pajoniiir_hcd_source_found)
unset(_pajoniiir_fifo_upstream)
unset(_pajoniiir_fifo_patched)
unset(_pajoniiir_upstream_at)
