idf_component_register(SRCS "web_server.c" "dns_server.c" "dns_reply.c" "web_api_helpers.c"
                       INCLUDE_DIRS "include"
                       REQUIRES "audio_engine" "control_link" "ui" "media_catalog" "esp_http_server" "deck_core" "controller_profile_manager" "p4_ota" "ota_manifest" "service_log" "sd_io_gate" "audio_recorder"
                       EMBED_TXTFILES "web/index.html" "web/style.css" "web/app.js")
