idf_component_register(
    SRCS "ota_manifest.c" "ota_manifest_crypto.c"
    INCLUDE_DIRS "include"
    REQUIRES mbedtls
    EMBED_FILES "keys/ddj_ota_release_public.der"
)
