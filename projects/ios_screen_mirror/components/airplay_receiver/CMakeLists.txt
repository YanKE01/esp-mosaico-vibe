set(RP "${CMAKE_CURRENT_LIST_DIR}/vendor/rpiplay/lib")
set(RP_SOURCES
    ${RP}/byteutils.c ${RP}/fairplay_playfair.c ${RP}/http_request.c
    ${RP}/http_response.c ${RP}/httpd.c ${RP}/logger.c ${RP}/mirror_buffer.c
    ${RP}/netutils.c ${RP}/pairing.c ${RP}/raop.c ${RP}/raop_buffer.c
    ${RP}/raop_ntp.c ${RP}/raop_rtp.c ${RP}/raop_rtp_mirror.c ${RP}/utils.c
    ${RP}/playfair/hand_garble.c ${RP}/playfair/modified_md5.c
    ${RP}/playfair/omg_hax.c ${RP}/playfair/playfair.c ${RP}/playfair/sap_hash.c
    ${RP}/llhttp/api.c ${RP}/llhttp/http.c ${RP}/llhttp/llhttp.c)
idf_component_register(
    SRCS ${RP_SOURCES} src/airplay_receiver.c src/esp_dnssd.c src/esp_posix.c
         src/ios_crypto.c src/ios_aes.c src/ed25519_ops.c src/plist.c src/airplay_packet.c
    INCLUDE_DIRS include
    PRIV_INCLUDE_DIRS ${RP} ${RP}/playfair ${RP}/llhttp src shim
    REQUIRES esp_common
    PRIV_REQUIRES mbedtls esp_hw_support esp_system pthread lwip mdns)
set_source_files_properties(${RP_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-w;-include;${CMAKE_CURRENT_LIST_DIR}/shim/esp_posix.h")
