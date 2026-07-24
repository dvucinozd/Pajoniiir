# Pure metadata parsing and configuration validation: no network, no radio, no
# dependencies. Split out from p4_ota_pull so the web layer can use the
# validators without dragging in wifi_link, which would close the cycle
# web_server -> p4_ota_pull -> wifi_link -> web_server.
idf_component_register(
    SRCS "p4_ota_pull_manifest.c"
         "p4_ota_pull_config.c"
    INCLUDE_DIRS "include"
)