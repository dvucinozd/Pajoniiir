# v246: vendored copy of esp-djlink 0.3.0 (https://github.com/kayrozen/esp-djlink,
# MIT, see LICENSE/README.md). Pure C99 codec - no lwIP, no FreeRTOS, no heap.
# The transport lives in components/dj_link; this component only parses
# and runs I/O-free state machines (NFS fetch client, 0.3.0).
idf_component_register(SRCS "src/djlink_packet.c"
                            "src/djlink_beat.c"
                            "src/djlink_status.c"
                            "src/djlink_claim.c"
                            "src/djlink_sync.c"
                            "src/djlink_media.c"
                            "src/djlink_mixer.c"
                            "src/djlink_dbserver.c"
                            "src/djlink_nfs.c"
                       INCLUDE_DIRS "include")
