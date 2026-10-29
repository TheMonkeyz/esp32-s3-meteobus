# The board's own code: the Waveshare ESP32-S3-Touch-AMOLED-1.75 (display.c, touch.c, audio.c, imu.c) or the
# ESP32-P4-Function-EV-Board (the *_p4.c files: MIPI-DSI EK79007, GT911, ES8311 only, no motion sensor; Wi-Fi through
# its ESP32-C6 with esp_hosted / esp_wifi_remote)
if(IDF_TARGET STREQUAL "esp32p4")
    set(board_srcs "display_p4.c" "touch_p4.c" "audio_p4.c" "imu_p4.c")
    set(board_reqs esp_lcd esp_lcd_ek79007 esp_driver_ledc esp_hw_support esp_mm esp_wifi_remote esp_hosted)
else()
    set(board_srcs "display.c" "touch.c" "audio.c" "imu.c")
    set(board_reqs esp_lcd esp_driver_spi)
endif()

idf_component_register(
    SRCS "main.c" ${board_srcs} "weather.c" "ui.c" "radar.c" "config.c" "lvgl_mem.c" "alerts.c" "pager.c" "i18n.c" "sound.c" "slide.c" "routes.c" "console.c" "services.c" "departures.c" "rtc_api.c" "favs.c" "bus_routes.c" "netq.c"
    INCLUDE_DIRS "."
    # forge_core, forge_net, forge_ota, forge_presence: espforge's components at the tag in idf_component.yml (Wi-Fi, web server,
    # updates, diagnostics, test console, languages, textfit, png_rows, screen dimming by presence...)
    REQUIRES lvgl forge_core forge_net forge_ota forge_presence esp_codec_dev esp_driver_i2s ${board_reqs} esp_driver_gpio esp_driver_i2c esp_wifi esp_netif esp_event
             nvs_flash esp_http_client esp_http_server json esp_timer esp_partition esp_psram spi_flash esp_app_format app_update
    EMBED_FILES "montserrat.ttf" "syllabics.ttf"
    EMBED_TXTFILES "web/index.html")

# lvgl_mem.c provides LVGL's allocator (CONFIG_LV_USE_CUSTOM_MALLOC). Nothing in main references it,
# so force the linker to keep it (liblvgl is scanned before libmain resolves these).
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u lv_mem_init" "-u lv_malloc_core" "-u lv_realloc_core" "-u lv_free_core")
