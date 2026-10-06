# The pinned usb_host_msc component tears down a device by deleting its
# completion semaphore first and then returning on the first cleanup error.
# A hot-unplug can therefore leave a half-destroyed device (or a transfer whose
# callback targets an already deleted semaphore). It also leaves device->xfer
# pointing at freed memory while growing the bulk-transfer buffer. Pajoniiir
# bounds every storage read to 8 KiB, so allocate that capacity once when the
# MSC device is installed and never replace the transfer object at runtime.
#
# Keep the dependency pin reproducible and apply a narrow, fail-closed source
# transform into build/. Remove this patch once the pinned dependency contains
# equivalent teardown and transfer-ownership guarantees.

idf_component_get_property(_pajoniiir_msc_dir espressif__usb_host_msc COMPONENT_DIR)
idf_component_get_property(_pajoniiir_msc_lib espressif__usb_host_msc COMPONENT_LIB)
set(_pajoniiir_msc_source "${_pajoniiir_msc_dir}/src/msc_host.c")

if(NOT EXISTS "${_pajoniiir_msc_source}")
    message(FATAL_ERROR
        "Pinned usb_host_msc source was not found: ${_pajoniiir_msc_source}")
endif()

set(_pajoniiir_msc_default_xfer_upstream [=[
#define DEFAULT_XFER_SIZE   (64) // Transfer size used for all transfers apart from SCSI read/write
]=])

set(_pajoniiir_msc_default_xfer_patched [=[
/* Pajoniiir caps every FatFs transaction to this capacity. Keeping one
 * transfer object for the complete device lifetime avoids a hot-unplug race
 * in the upstream grow-by-free-and-reallocate path. */
#define DEFAULT_XFER_SIZE   (8 * 1024)
]=])

set(_pajoniiir_msc_deinit_upstream [=[
static esp_err_t msc_deinit_device(msc_device_t *dev, bool install_failed)
{
    MSC_ENTER_CRITICAL();
    MSC_RETURN_ON_FALSE_CRITICAL( dev, ESP_ERR_INVALID_STATE );
    STAILQ_REMOVE(&s_msc_driver->devices_tailq, dev, msc_host_device, tailq_entry);
    MSC_EXIT_CRITICAL();

    if (dev->transfer_done) {
        vSemaphoreDelete(dev->transfer_done);
    }
    if (install_failed) {
        // Error code is unchecked, as it's unknown at what point installation failed.
        usb_host_interface_release(s_msc_driver->client_handle, dev->handle, dev->config.iface_num);
        usb_host_device_close(s_msc_driver->client_handle, dev->handle);
        usb_host_transfer_free(dev->xfer);
    } else {
        MSC_RETURN_ON_ERROR( usb_host_interface_release(s_msc_driver->client_handle, dev->handle, dev->config.iface_num) );
        MSC_RETURN_ON_ERROR( usb_host_device_close(s_msc_driver->client_handle, dev->handle) );
        MSC_RETURN_ON_ERROR( usb_host_transfer_free(dev->xfer) );
    }

    free(dev);
    return ESP_OK;
}
]=])

set(_pajoniiir_msc_deinit_patched [=[
static esp_err_t msc_deinit_device(msc_device_t *dev, bool install_failed)
{
    MSC_ENTER_CRITICAL();
    MSC_RETURN_ON_FALSE_CRITICAL( dev && s_msc_driver, ESP_ERR_INVALID_STATE );
    MSC_EXIT_CRITICAL();

    /* A transfer callback owns transfer_done through dev->context. Refuse to
     * dismantle that context if the Host Library cannot retire the transfer. */
    if (dev->xfer) {
        esp_err_t rc = usb_host_transfer_free(dev->xfer);
        if (rc != ESP_OK) {
            return rc;
        }
        dev->xfer = NULL;
    }

    MSC_ENTER_CRITICAL();
    STAILQ_REMOVE(&s_msc_driver->devices_tailq, dev, msc_host_device, tailq_entry);
    MSC_EXIT_CRITICAL();

    /* Detach every remaining allocator-backed resource before invoking APIs
     * which can publish another gone-device edge. Cleanup runs to completion
     * even if interface release or close reports an already-gone device. */
    usb_device_handle_t handle = dev->handle;
    dev->handle = NULL;

    esp_err_t first_error = ESP_OK;
    if (handle) {
        esp_err_t rc = usb_host_interface_release(
            s_msc_driver->client_handle, handle, dev->config.iface_num);
        if (!install_failed && rc != ESP_OK && first_error == ESP_OK) {
            first_error = rc;
        }

        rc = usb_host_device_close(s_msc_driver->client_handle, handle);
        if (!install_failed && rc != ESP_OK && first_error == ESP_OK) {
            first_error = rc;
        }
    }
    if (dev->transfer_done) {
        vSemaphoreDelete(dev->transfer_done);
        dev->transfer_done = NULL;
    }

    free(dev);
    return first_error;
}
]=])

set(_pajoniiir_msc_resize_upstream [=[
    if (xfer->data_buffer_size < transfer_size) {
        // The allocated buffer is not large enough -> realloc
        MSC_RETURN_ON_ERROR( usb_host_transfer_free(xfer) );
        MSC_RETURN_ON_ERROR( usb_host_transfer_alloc(transfer_size, 0, &device->xfer) );
        xfer = device->xfer;
    }
]=])

set(_pajoniiir_msc_resize_patched [=[
    if (xfer == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xfer->data_buffer_size < transfer_size) {
        /* The application guarantees bounded disk transactions. Fail closed
         * if that contract is ever violated instead of changing transfer
         * ownership while a disconnect callback can run. */
        return ESP_ERR_INVALID_SIZE;
    }
]=])

file(READ "${_pajoniiir_msc_source}" _pajoniiir_msc_contents)
foreach(_pajoniiir_msc_block IN ITEMS default_xfer deinit resize)
    string(FIND "${_pajoniiir_msc_contents}"
           "${_pajoniiir_msc_${_pajoniiir_msc_block}_upstream}"
           _pajoniiir_msc_block_at)
    if(_pajoniiir_msc_block_at EQUAL -1)
        message(FATAL_ERROR
            "usb_host_msc msc_host.c no longer matches the pinned "
            "${_pajoniiir_msc_block} block; review the Pajoniiir teardown patch")
    endif()
    string(REPLACE
           "${_pajoniiir_msc_${_pajoniiir_msc_block}_upstream}"
           "${_pajoniiir_msc_${_pajoniiir_msc_block}_patched}"
           _pajoniiir_msc_contents "${_pajoniiir_msc_contents}")
endforeach()

set(_pajoniiir_msc_generated_dir "${CMAKE_BINARY_DIR}/pajoniiir_usb_host_msc")
set(_pajoniiir_msc_generated_source
    "${_pajoniiir_msc_generated_dir}/msc_host.c")
file(MAKE_DIRECTORY "${_pajoniiir_msc_generated_dir}")
file(WRITE "${_pajoniiir_msc_generated_source}" "${_pajoniiir_msc_contents}")
set_source_files_properties("${_pajoniiir_msc_generated_source}"
                            PROPERTIES GENERATED TRUE)

get_target_property(_pajoniiir_msc_sources "${_pajoniiir_msc_lib}" SOURCES)
set(_pajoniiir_msc_replacement_sources)
set(_pajoniiir_msc_source_found FALSE)
foreach(_pajoniiir_msc_component_source IN LISTS _pajoniiir_msc_sources)
    get_filename_component(_pajoniiir_msc_component_source_abs
                           "${_pajoniiir_msc_component_source}"
                           ABSOLUTE BASE_DIR "${_pajoniiir_msc_dir}")
    if(_pajoniiir_msc_component_source_abs STREQUAL _pajoniiir_msc_source)
        list(APPEND _pajoniiir_msc_replacement_sources
             "${_pajoniiir_msc_generated_source}")
        set(_pajoniiir_msc_source_found TRUE)
    else()
        list(APPEND _pajoniiir_msc_replacement_sources
             "${_pajoniiir_msc_component_source}")
    endif()
endforeach()

if(NOT _pajoniiir_msc_source_found)
    message(FATAL_ERROR
        "Could not replace usb_host_msc msc_host.c in the component source list")
endif()

set_property(TARGET "${_pajoniiir_msc_lib}" PROPERTY SOURCES
             "${_pajoniiir_msc_replacement_sources}")
message(STATUS "Using generated Pajoniiir usb_host_msc teardown-patched source")

unset(_pajoniiir_msc_contents)
unset(_pajoniiir_msc_source)
unset(_pajoniiir_msc_dir)
unset(_pajoniiir_msc_lib)
unset(_pajoniiir_msc_sources)
unset(_pajoniiir_msc_replacement_sources)
unset(_pajoniiir_msc_component_source)
unset(_pajoniiir_msc_component_source_abs)
unset(_pajoniiir_msc_generated_dir)
unset(_pajoniiir_msc_generated_source)
unset(_pajoniiir_msc_source_found)
unset(_pajoniiir_msc_block)
unset(_pajoniiir_msc_block_at)
unset(_pajoniiir_msc_default_xfer_upstream)
unset(_pajoniiir_msc_default_xfer_patched)
unset(_pajoniiir_msc_deinit_upstream)
unset(_pajoniiir_msc_deinit_patched)
unset(_pajoniiir_msc_resize_upstream)
unset(_pajoniiir_msc_resize_patched)
