# Storage recovery must never power-cycle USB0 after a device connection has
# reached the HCD, even if that device has not yet completed enumeration.  The
# public Host Library device count cannot express this in the dual-root image:
# FLX4 on USB1 keeps the global count non-zero, while a newly attached USB0
# stick is deliberately absent from that count until enumeration completes.
#
# Add a narrow indexed operation to the pinned esp-usb sources.  Its HCD check
# and POWER_OFF command execute under the same HCD critical section, closing
# the check/use race that previously let recovery invalidate enum.c state and
# abort usb_hostd.  Keep the source transform fail-closed and generated under
# build/ so managed_components stays reproducible.

idf_component_get_property(_pajoniiir_idle_usb_dir usb COMPONENT_DIR)
idf_component_get_property(_pajoniiir_idle_usb_lib usb COMPONENT_LIB)
get_target_property(_pajoniiir_idle_usb_sources "${_pajoniiir_idle_usb_lib}" SOURCES)

set(_pajoniiir_idle_hub_source
    "${_pajoniiir_idle_usb_dir}/src/hub.c")
set(_pajoniiir_idle_host_source
    "${_pajoniiir_idle_usb_dir}/src/usb_host.c")

set(_pajoniiir_idle_hcd_upstream [=[
    return ret;
}

hcd_port_state_t hcd_port_get_state(hcd_port_handle_t port_hdl)
]=])

set(_pajoniiir_idle_hcd_patched [=[
    return ret;
}

/* Pajoniiir dual-root recovery primitive.  A connection ISR and this
 * conditional POWER_OFF serialize on the HCD critical section, so recovery
 * cannot tear down a port once attach/enumeration has begun. */
esp_err_t pajoniiir_hcd_port_power_off_if_disconnected(
    hcd_port_handle_t port_hdl)
{
    if (port_hdl == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_ERR_INVALID_STATE;
    port_t *port = (port_t *)port_hdl;
    xSemaphoreTake(port->port_mux, portMAX_DELAY);
    HCD_ENTER_CRITICAL();
    if (s_port_inited[port->periph_idx] && !port->flags.event_pending) {
        if (port->state == HCD_PORT_STATE_DISCONNECTED) {
            port->flags.cmd_processing = 1;
            ret = _port_cmd_power_off(port);
            port->flags.cmd_processing = 0;
        } else if (port->state != HCD_PORT_STATE_NOT_POWERED) {
            ret = ESP_ERR_NOT_FINISHED;
        }
    } else if (s_port_inited[port->periph_idx] && port->flags.event_pending) {
        ret = ESP_ERR_NOT_FINISHED;
    }
    HCD_EXIT_CRITICAL();
    xSemaphoreGive(port->port_mux);
    return ret;
}

hcd_port_state_t hcd_port_get_state(hcd_port_handle_t port_hdl)
]=])

set(_pajoniiir_idle_hub_upstream [=[
    return ESP_OK;
}

bool hub_root_is_suspended(void)
]=])

set(_pajoniiir_idle_hub_patched [=[
    return ESP_OK;
}

extern esp_err_t pajoniiir_hcd_port_power_off_if_disconnected(
    hcd_port_handle_t port_hdl);

esp_err_t pajoniiir_hub_root_port_stop_if_disconnected(int port_num)
{
    if (port_num < 0 || port_num >= HCD_NUM_PORTS) {
        return ESP_ERR_INVALID_ARG;
    }

    HUB_DRIVER_ENTER_CRITICAL();
    HUB_DRIVER_CHECK_FROM_CRIT(p_hub_driver_obj != NULL,
                               ESP_ERR_INVALID_STATE);
    root_hub_port_t *root_port =
        &p_hub_driver_obj->root_hub_ports[port_num];
    HUB_DRIVER_CHECK_FROM_CRIT(root_port->constant.hdl != NULL,
                               ESP_ERR_NOT_FOUND);
    hcd_port_handle_t root_port_hdl = root_port->constant.hdl;
    HUB_DRIVER_EXIT_CRITICAL();

    const esp_err_t ret =
        pajoniiir_hcd_port_power_off_if_disconnected(root_port_hdl);
    if (ret != ESP_OK) {
        return ret;
    }

    HUB_DRIVER_ENTER_CRITICAL();
    root_port->dynamic.state = ROOT_PORT_STATE_NOT_POWERED;
    HUB_DRIVER_EXIT_CRITICAL();
    return ESP_OK;
}

bool hub_root_is_suspended(void)
]=])

set(_pajoniiir_idle_host_upstream [=[
esp_err_t usb_host_lib_set_root_port_power_by_index(uint8_t root_port_index, bool enable)
{
    if (enable) {
        return hub_root_port_start((int)root_port_index);
    }
    return hub_root_port_stop((int)root_port_index);
}

esp_err_t usb_host_lib_root_port_suspend(void)
]=])

set(_pajoniiir_idle_host_patched [=[
esp_err_t usb_host_lib_set_root_port_power_by_index(uint8_t root_port_index, bool enable)
{
    if (enable) {
        return hub_root_port_start((int)root_port_index);
    }
    return hub_root_port_stop((int)root_port_index);
}

extern esp_err_t pajoniiir_hub_root_port_stop_if_disconnected(int port_num);

esp_err_t usb_host_lib_power_off_root_port_if_idle_by_index(
    uint8_t root_port_index)
{
    return pajoniiir_hub_root_port_stop_if_disconnected(
        (int)root_port_index);
}

esp_err_t usb_host_lib_root_port_suspend(void)
]=])

set(_pajoniiir_idle_generated_dir
    "${CMAKE_BINARY_DIR}/pajoniiir_usb_idle_recovery")
file(MAKE_DIRECTORY "${_pajoniiir_idle_generated_dir}")

set(_pajoniiir_idle_replacement_sources)
set(_pajoniiir_idle_hcd_found FALSE)
set(_pajoniiir_idle_hub_found FALSE)
set(_pajoniiir_idle_host_found FALSE)

foreach(_pajoniiir_idle_source IN LISTS _pajoniiir_idle_usb_sources)
    get_filename_component(_pajoniiir_idle_source_abs
                           "${_pajoniiir_idle_source}" ABSOLUTE
                           BASE_DIR "${_pajoniiir_idle_usb_dir}")
    get_filename_component(_pajoniiir_idle_source_name
                           "${_pajoniiir_idle_source_abs}" NAME)

    set(_pajoniiir_idle_kind "")
    if(_pajoniiir_idle_source_name STREQUAL "hcd_dwc.c")
        set(_pajoniiir_idle_kind "hcd")
        set(_pajoniiir_idle_hcd_found TRUE)
    elseif(_pajoniiir_idle_source_abs STREQUAL _pajoniiir_idle_hub_source)
        set(_pajoniiir_idle_kind "hub")
        set(_pajoniiir_idle_hub_found TRUE)
    elseif(_pajoniiir_idle_source_abs STREQUAL _pajoniiir_idle_host_source)
        set(_pajoniiir_idle_kind "host")
        set(_pajoniiir_idle_host_found TRUE)
    endif()

    if(_pajoniiir_idle_kind STREQUAL "")
        list(APPEND _pajoniiir_idle_replacement_sources
             "${_pajoniiir_idle_source}")
        continue()
    endif()

    file(READ "${_pajoniiir_idle_source_abs}" _pajoniiir_idle_contents)
    string(FIND "${_pajoniiir_idle_contents}"
           "${_pajoniiir_idle_${_pajoniiir_idle_kind}_upstream}"
           _pajoniiir_idle_block_at)
    if(_pajoniiir_idle_block_at EQUAL -1)
        message(FATAL_ERROR
            "esp-usb ${_pajoniiir_idle_source_name} no longer matches the "
            "pinned idle-recovery block; review the Pajoniiir patch")
    endif()
    string(REPLACE
           "${_pajoniiir_idle_${_pajoniiir_idle_kind}_upstream}"
           "${_pajoniiir_idle_${_pajoniiir_idle_kind}_patched}"
           _pajoniiir_idle_contents "${_pajoniiir_idle_contents}")

    set(_pajoniiir_idle_generated_source
        "${_pajoniiir_idle_generated_dir}/${_pajoniiir_idle_source_name}")
    file(WRITE "${_pajoniiir_idle_generated_source}"
         "${_pajoniiir_idle_contents}")
    set_source_files_properties("${_pajoniiir_idle_generated_source}"
                                PROPERTIES GENERATED TRUE)
    list(APPEND _pajoniiir_idle_replacement_sources
         "${_pajoniiir_idle_generated_source}")
endforeach()

if(NOT _pajoniiir_idle_hcd_found OR NOT _pajoniiir_idle_hub_found OR
   NOT _pajoniiir_idle_host_found)
    message(FATAL_ERROR
        "Could not replace all esp-usb HCD, Hub, and Host Library sources "
        "for indexed idle recovery")
endif()

set_property(TARGET "${_pajoniiir_idle_usb_lib}" PROPERTY SOURCES
             "${_pajoniiir_idle_replacement_sources}")
message(STATUS
    "Using generated Pajoniiir esp-usb indexed idle-recovery sources")

unset(_pajoniiir_idle_usb_dir)
unset(_pajoniiir_idle_usb_lib)
unset(_pajoniiir_idle_usb_sources)
unset(_pajoniiir_idle_source)
unset(_pajoniiir_idle_source_abs)
unset(_pajoniiir_idle_source_name)
unset(_pajoniiir_idle_kind)
unset(_pajoniiir_idle_contents)
unset(_pajoniiir_idle_block_at)
unset(_pajoniiir_idle_generated_dir)
unset(_pajoniiir_idle_generated_source)
unset(_pajoniiir_idle_replacement_sources)
unset(_pajoniiir_idle_hcd_found)
unset(_pajoniiir_idle_hub_found)
unset(_pajoniiir_idle_host_found)
unset(_pajoniiir_idle_hub_source)
unset(_pajoniiir_idle_host_source)
unset(_pajoniiir_idle_hcd_upstream)
unset(_pajoniiir_idle_hcd_patched)
unset(_pajoniiir_idle_hub_upstream)
unset(_pajoniiir_idle_hub_patched)
unset(_pajoniiir_idle_host_upstream)
unset(_pajoniiir_idle_host_patched)
