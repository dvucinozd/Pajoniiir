idf_component_register(SRCS "web_server.c" "dns_server.c" "dns_reply.c" "web_api_helpers.c"
                       INCLUDE_DIRS "include"
                       REQUIRES "audio_engine" "control_link" "ui" "media_catalog" "esp_http_server" "deck_core"
                       EMBED_TXTFILES "web/index.html" "web/style.css" "web/app.js")
