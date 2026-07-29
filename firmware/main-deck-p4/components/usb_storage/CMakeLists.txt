idf_component_register(
    SRCS "usb_storage.c" "usb_storage_session.c" "usb_storage_recovery.c"
         "usb_media_partition.c" "usb_media_mount.c"
         "usb_dwc_hal_compat.c"
    INCLUDE_DIRS "include"
    REQUIRES log usb usb_host_msc fatfs media_io_gate hal
    WHOLE_ARCHIVE
)

# The DWC HAL asserts that every channel error also raised CHHLTD, while its own
# header documents BNAINTR as the one error that does not - and BNAINTR is in the
# error mask. With HAL assertions at level 2 that combination panics.
#
# It is reached on this board: a 30-second P4 capture hit it, and bench-notes
# records the trigger as streaming from USB during playback, which is exactly
# what the bounded compressed cache now does on every deck. Wrap only the
# interrupt decoder so that one documented condition takes the HCD's recoverable
# pipe-error path; every other missing-halt case still aborts, and HAL assertions
# stay enabled everywhere else in the system.
target_link_options(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=usb_dwc_hal_chan_decode_intr")
