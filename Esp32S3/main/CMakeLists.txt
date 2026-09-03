idf_component_register(
  SRCS
    "app_main.c"
    "display_task.c"
    "protocol/frame_parser.c"
    "protocol/config.c"
    "render/geo.c"
    "comm/comm_if.c"
    "comm/wifi_sta.c"
    "comm/softap_prov.c"
    "comm/tcp_server.c"
    "comm/mdns_service.c"
    "comm/ble_fallback.c"
    "lcd/lcd_driver.c"
    "font/font.c"
  INCLUDE_DIRS "." "protocol" "render" "comm" "lcd" "font"
)
