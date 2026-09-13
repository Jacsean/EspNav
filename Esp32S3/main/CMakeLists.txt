idf_component_register(
  SRCS
    "app_main.c"
    "display_task.c"
    "protocol/frame_parser.c"
    "protocol/config.c"
    "protocol/nav_frame.c"
    "render/geo.c"
    "render/lcd_fb.c"
    "render/render_nav.c"
    "comm/comm_if.c"
    "comm/wifi_ap.c"
    "comm/tcp_server.c"
    "comm/wifi_sta.c"
    "comm/softap_prov.c"
    "comm/tcp_server.c"
    "comm/mdns_service.c"
    "comm/ble_fallback.c"
    "lcd/lcd_driver.c"
    "lcd/ili9341.c"
    "font/font.c"
  INCLUDE_DIRS "." "protocol" "render" "comm" "lcd" "font"
  REQUIRES esp_driver_spi esp_driver_gpio esp_driver_ledc esp_psram esp_wifi esp_netif nvs_flash json lwip
)
