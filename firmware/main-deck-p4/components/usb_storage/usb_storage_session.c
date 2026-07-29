#include "usb_storage_session.h"

#include <string.h>

void usb_storage_session_reset(usb_storage_session_t *session)
{
    if (!session) {
        return;
    }
    memset(session, 0, sizeof(*session));
}

usb_storage_connect_result_t usb_storage_session_on_connect(
    usb_storage_session_t *session,
    uint8_t dev_addr)
{
    if (!session || dev_addr == 0u) {
        return USB_STORAGE_CONNECT_IGNORED_SECONDARY;
    }
    if (session->connected) {
        return session->dev_addr == dev_addr
                   ? USB_STORAGE_CONNECT_DUPLICATE
                   : USB_STORAGE_CONNECT_IGNORED_SECONDARY;
    }

    session->connected = true;
    session->mounted = false;
    session->dev_addr = dev_addr;
    session->accepted_handle = 0u;
    session->epoch++;
    return USB_STORAGE_CONNECT_ACCEPTED;
}

bool usb_storage_session_bind_handle(usb_storage_session_t *session,
                                     uint32_t epoch,
                                     uint8_t dev_addr,
                                     uintptr_t handle)
{
    if (!usb_storage_session_matches(session, epoch, dev_addr) || handle == 0u) {
        return false;
    }
    if (session->accepted_handle != 0u &&
        session->accepted_handle != handle) {
        return false;
    }
    session->accepted_handle = handle;
    return true;
}

void usb_storage_session_release_handle(usb_storage_session_t *session,
                                        uintptr_t handle)
{
    if (!session || handle == 0u) {
        return;
    }
    if (session->accepted_handle == handle) {
        session->accepted_handle = 0u;
    }
}

usb_storage_disconnect_result_t usb_storage_session_on_disconnect(
    usb_storage_session_t *session,
    uintptr_t handle)
{
    if (!session || !session->connected) {
        return USB_STORAGE_DISCONNECT_ALREADY_INACTIVE;
    }

    /* Before msc_host_install_device() returns, the storage task cannot yet
     * publish the opaque handle. The driver can still report that in-flight
     * primary disappearing, so an unbound session accepts the disconnect.
     * Once bound, only the owner handle may end the session. */
    if (session->accepted_handle != 0u &&
        session->accepted_handle != handle) {
        return USB_STORAGE_DISCONNECT_IGNORED_FOREIGN;
    }

    session->connected = false;
    session->mounted = false;
    session->dev_addr = 0u;
    session->accepted_handle = 0u;
    session->epoch++;
    return USB_STORAGE_DISCONNECT_ACCEPTED;
}

bool usb_storage_session_matches(const usb_storage_session_t *session,
                                 uint32_t epoch,
                                 uint8_t dev_addr)
{
    return session &&
           session->connected &&
           session->epoch == epoch &&
           session->dev_addr == dev_addr;
}

bool usb_storage_session_commit_mounted(usb_storage_session_t *session,
                                        uint32_t epoch,
                                        uint8_t dev_addr)
{
    if (!usb_storage_session_matches(session, epoch, dev_addr) ||
        session->accepted_handle == 0u) {
        return false;
    }
    session->mounted = true;
    return true;
}

void usb_storage_session_mark_unmounted(usb_storage_session_t *session)
{
    if (session) {
        session->mounted = false;
    }
}
