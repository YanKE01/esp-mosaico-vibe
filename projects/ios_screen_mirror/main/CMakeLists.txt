idf_component_register(
    SRCS main.c mirror_wifi.c mirror_video.c mirror_h264.c mirror_pixels.c mirror_lcd.c
         mirror_usb_console.c
    INCLUDE_DIRS "."
    REQUIRES airplay_receiver esp-mosaico-bsp
    PRIV_REQUIRES openh264_decoder esp_wifi esp_event esp_netif nvs_flash esp_timer
                  esp_lcd esp_lcd_touch esp_mm mdns esp_tinyusb)
