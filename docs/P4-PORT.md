# MeteoBus on the Guition JC1060P470C (ESP32-P4, 7" 1024x600)

Branch `port/esp32-p4`, started 2026-10-29. The same firmware as the round AMOLED (Waveshare ESP32-S3-Touch-AMOLED-1.75),
built for a second board: the target (`IDF_TARGET=esp32p4`) picks the board's own files and a landscape layout. The
round display's code and behaviour are unchanged. No release has been made from this branch: no tags, no CI build yet
(see "Building").

## Status (2026-10-29)

Works on the board, checked on the device (log, console screenshots, the user's eyes):

- Every screen in landscape: weather (five forecast days as rows, full day names; a tap on a row opens its hours),
  stop pages, bus map, extras, status, settings (rows on the left, the settings page's QR code on the right), hourly
  view, radar, weather alert, update, Wi-Fi setup.
- Moves at the panel's rate: swipes 62-64 fps, gap max 16 ms, no tearing; hourly list scrolls ~58 fps; zooms as on the
  AMOLED.
- Wi-Fi through the board's ESP32-C6 (esp-hosted), the settings page on the home network at `https://<ip>/` (the PC
  reaches it; the user's phone doesn't, see "Open"), updates from the release site (not tried end to end).
- Speaker (alert sounds, the settings page's test), microphone (presence dimming: quiet room -57.5 dB, speech -33 dB
  against the -50 dB threshold), touch.

Left out, as the user chose ("drop what's missing"): Easy Connect (DPP doesn't go through the C6), the motion sensor
(there is none), frequency scaling and the real screen-off (`CONFIG_PM_ENABLE=n`, no `display_sleep()` yet).

## The board

Guition JC1060P470C (the user's: `_I_W_Y`, in a black case with an Ethernet port; no label on the back). ESP32-P4 chip
revision **v1.3**, 16 MB flash, 32 MB PSRAM; an ESP32-C6 for Wi-Fi, linked by SDIO. On the PC it is **COM7** (native
USB, VID 303A).

| Part | How it's wired | Notes |
| --- | --- | --- |
| Panel | MIPI-DSI, 2 lanes, JD9165, 1024x600; LDO channel 3 at 2500 mV | The **V2** panel (2026): another init table (Gil Tal's ESP32-P4-Internet-Radio, MIT), reset on GPIO 0. The V1 table gives a white, streaked band. `CONFIG_METEOBUS_P4_PANEL_V2` |
| Backlight | GPIO 23, LEDC PWM | On after LVGL's first frame (a cyan field at boot before) |
| Touch | GT911 on I2C1 (SDA 7, SCL 8), 0x5D or 0x14 | Reports in 800x480, scaled; a report every 20 ms. Its config (v99) has a checksum that isn't the GT911 formula: left alone, never written |
| Wi-Fi | ESP32-C6, SDIO: CLK 18, CMD 19, D0-D3 14-17, C6 reset GPIO 54 | SDIO at 10 MHz: at 40 the V2 panel streaks |
| Audio | ES8311 (I2C 0x18 on the touch bus), I2S0: MCLK 13, BCLK 12, WS 10, DOUT 9, DIN 48 | One codec both ways: its ADC is the microphone (behind the case's round holes) |
| Amplifier | NS4150, enable **GPIO 11** | Not GPIO 20 (a header pin): the first build was silent |
| Speaker | 2-pin MX1.25 socket | None in the case: the user plugged one in. The other 2-pin socket of the same size is the **battery's** |
| Other I2C | 0x32, 0x36 | Probably the RTC and a battery gauge (unused) |
| BOOT button | GPIO 35 | |

There is no ES7210 on the bus, whatever the schematic shows (a scan found 14 18 32 36 5D). Pins were confirmed against
a working ESPHome setup for this board (github.com/charrus/Guition-esp32-p4-jc1060p470-Sci-fi-dashboard) and the
Home Assistant forum's thread on it.

## Code

`main/CMakeLists.txt` picks the board files by target: `display_p4.c`, `touch_p4.c`, `imu_p4.c` (a stub) instead of
`display.c`, `touch.c`, `imu.c` (`audio.c` holds both boards' pins for espforge's forge_audio). Everything else is shared; `display.h` says which layout:

- `DISP_ROUND` 1 on the AMOLED, 0 here: `ui.c` chooses the landscape layout's sizes and positions with it (fonts ~1.3x,
  `ICON_HERO` 128, the forecast rows `FC_*`, the hourly columns `HC_*`, the bus map's pills `BM_*`, the alert's `AL_*`).
- `DISP_SWAP` 1 on the AMOLED (the panel wants big-endian RGB565), 0 here: `slide.c` copies without swapping.
- `DISP_W/H` the LVGL screen (1024x600, the whole panel here), `DISP_OX/OY` its place on the panel (0 here).

### Display (`display_p4.c`)

Two DPI frame buffers in PSRAM. LVGL draws into the one shown (partial redraws, 32-line bands); a move (`slide.c`)
builds each frame in the other and swaps them (`display_frame_begin()` / `display_frame_done()`: the swap happens at the
end of the panel's refresh, `on_refresh_done` is the vsync), so moves don't tear. Copies go through the **2D-DMA**
(`esp_async_fbcpy`, esp_lcd's private header: `priv_include` in CMakeLists), black fills through the PPA. The DSI's
frame DMA (DW-GDMA) has the highest priority on the AXI bus (`axi_icm_ll_set_dw_gdma_qos_arbiter_prio`): underruns
showed as blue/cyan flashes. Measured for one 1024x600 frame (console `blitbench`):

| Copy | Time |
| --- | --- |
| CPU memcpy, PSRAM to PSRAM | 26-28 ms |
| PPA scale engine at scale 1 | 26.5 ms |
| PPA fill | 3.8 ms |
| async_memcpy, one block / row by row | 11.6 / 56 ms |
| 2D-DMA (`esp_async_fbcpy`) | ~8 ms |

**White flashes (fixed 2026-10-30).** ESP-IDF's DPI driver restarts the panel's frame DMA from an interrupt after every
frame, and the gap between frames is ~0.9 ms. Anything that holds interrupts off longer, on the core that serves that
interrupt or on the other core while it waits for the same lock, leaves the panel without a picture for a frame: a
full-screen white flash, every few minutes. Found by timing (console `walk`): a walk of the PSRAM heap (any
`heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)`: diag's statistics, `slide.c`'s `room_for()`) holds the heap's
lock 2.3 ms, and `uxTaskGetSystemState()` (diag's per-task lines, which measure every stack) 6.1 ms. So: the panel is
set up from a task on core 1 (its interrupts are served there), PSRAM's largest free block is its free size
(`--wrap=heap_caps_get_largest_free_block`, `display_p4.c`), and diag has no per-task lines here
(`CONFIG_FORGE_DIAG_TASK_STATS=n`, espforge patch). Not the cause, tried on the way: the power supply (an 18 W
charger), Wi-Fi transmit power (11 dBm), code in PSRAM (`CONFIG_SPIRAM_XIP_FROM_PSRAM`), memory bandwidth (no
underrun counted). `flags.disable_lp` breaks this panel (streaks): keep the link's low-power mode.

List scrolls use the 2D-DMA too (`scroll_frame_dma()`: the frame from the picture's blocks in the buffer not shown).
Drags go on at the finger's last speed between two GT911 reports, at most 20 ms ahead (landscape only).

### Audio (`audio.c`, `sound.c`)

Since MeteoBus v0.3.2-rc.1 the codec code is espforge's forge_audio (its `FORGE_AUDIO_ES8311_BOTH` layout is this
port's `audio_p4.c` of v0.3.0-v0.3.1); `main/audio.c` keeps only the pins, for both boards. One `esp_codec_dev` for the ES8311, both ways (`ESP_CODEC_DEV_TYPE_IN_OUT`, `WORK_MODE_BOTH`, `no_dac_ref`), opened once
under a lock and kept open: `es8311_open()` rewrites the chip's set-up, so a second device opened at each chime would stop
the microphone, and the sound and presence tasks start together (two devices were created before the lock).
`audio_mic_open()` sets the input gain; `audio_speaker_dev()` gives `sound.c` the device, and on the P4 `sound.c` switches
the amplifier (GPIO 11) itself around each sound instead of opening and closing the codec.

### Wi-Fi and the settings page

esp_wifi's API through `esp_wifi_remote` 1.6.5 and `esp_hosted` 2.12.13 (host). `CONFIG_ESP_HOSTED_SDIO_TX/RX_Q_SIZE` 12
(the default 20 + 20 took the RAM FreeRTOS needed for the idle task: a boot loop).

The C6 came with Guition's **esp-hosted 2.3.2** with "network split": it kept every connection from the home network for
its own lwIP (nothing listening there), so the settings page was unreachable from any other device. It now runs
**esp-hosted 2.12.13** (built from esp-hosted-mcu's `slave/` for the C6, SDIO, no network split), installed through the
P4 (the C6 has no USB of its own here):

1. Build the C6 image, pad it with 4 KB of 0xFF in front (esptool refuses to write another chip's image) and put it at
   `p4\firmware\c6.bin` on the PC (the one used is kept in `p4\c6\`, not in git).
2. Flash: the helper also writes `c6.bin` to the P4's spare app slot (ota_1, 0x310000). Then remove it from `firmware\`.
3. Console `c6update SIZE` (the image's size): streams it from ota_1 (offset 0 or 4096, found by the image's magic) in
   1500-byte pieces through `esp_hosted_slave_ota_begin/write/end`; the C6 switches only once the image checks out, then
   the P4 restarts. ~12 s for 1.2 MB.

espforge needed four changes, carried in `p4/espforge-p4.patch` (against espforge v0.3.0) until espforge v0.6.0 took
them (2026-10-31; MeteoBus v0.3.2-rc.1 builds on that tag, the patch and `main/forge_compat.c` are gone): diag.c's
crash summary for RISC-V, forge_net without Easy Connect when DPP isn't there (`net_dpp_supported()`),
`CONFIG_FORGE_HTTPS_PORT` (443 by default; `web_port()` adds the port to the addresses forge_net and ui.c write), and
`CONFIG_FORGE_DIAG_TASK_STATS` (diag's per-task lines, off here; v0.6.0 also has `CONFIG_FORGE_DIAG_PSRAM_LARGEST`,
the heap line's PSRAM walk, off here too). The P4 used 50443 for a while; it is back on 443 since the C6's update.

### Radar

Map-cache slots of 1.25 MB (`partitions_p4.csv`: mapcache 0x620000, 0x8C0000), lightning positions in 4 px units, the
cache read yielding every 64 KB (one long read tripped the task watchdog).

### sdkconfig (`sdkconfig.defaults.esp32p4`)

Read after `sdkconfig.defaults`. Chip revision below v3 (`CONFIG_ESP32P4_SELECTS_REV_LESS_V3`: IDF 5.5.3+ builds for
v3.x, which won't boot on v1.3), DIO flash, CPU 360 MHz, PSRAM 200 MHz, PM off, esp-hosted (queues 12, SDIO 10 MHz),
BOYA flash support, brownout level 5 (the amplifier's peaks on weak USB), the V2 panel, `partitions_p4.csv`, lwIP
statistics (console `netstat`), task watchdog 10 s (TLS handshakes in software and the radar's map reads at start-up
held core 0 past 5 s).

## Building

Since MeteoBus v0.3.2-rc.1 (2026-10-31) `main/idf_component.yml` lists the P4's components (`esp_lcd_jd9165` 2.0.2,
`esp_wifi_remote` 1.6.5, `esp_hosted` 2.12.13) under a target rule, so the PC builds it with the component manager:

    idf.py -B build\p4 -D IDF_TARGET=esp32p4 -D SDKCONFIG=build\p4\sdkconfig build

(the AMOLED's build skips them). Then copy `build\p4\meteobus.bin`, `bootloaderootloader.bin` and
`partition_table\partition-table.bin` to `p4irmware\` and run `p4/flash.sh MD5 SECS`. There is no P4 build in CI
yet. The cloud session built with the component manager off, as CLAUDE.md's "Cloud build recipe", plus:

- the RISC-V toolchain (`riscv32-esp-elf`) on the PATH, `ESP_IDF_VERSION=5.5` in the environment (esp_wifi_remote's
  Kconfig needs it), `ESP_ROM_ELF_DIR` set;
- vendored components in `EXTRA_COMPONENT_DIRS`: lvgl 9.2.2, esp_codec_dev 1.6.2, esp_hosted 2.12.13, esp_wifi_remote
  1.6.5, esp_lcd_jd9165 2.0.2 (its CMakeLists and esp_lcd_ek79007's used the component manager's version macros:
  patched to build without it), and espforge's components at the tag in `main/idf_component.yml` (v0.6.0 or newer:
  the patch is gone since MeteoBus v0.3.2-rc.1);
- `p4/esp-idf-dpi-underrun.patch` on ESP-IDF (optional): the DSI bridge's interrupt counts underruns, and
  `display_p4.c` logs `panel underrun (N so far)`; ESP-IDF's own message for them goes to UART0, which nothing reads on
  this board. Without the patch the count is simply absent (a weak symbol);
- `cmake -S . -B build/p4 -G Ninja -DIDF_TARGET=esp32p4 -DSDKCONFIG=build/p4/sdkconfig ...`, then `ninja -C build/p4`.

## Flashing and testing on the PC

`p4\start_p4_helper.bat` starts `p4\flash_helper_p4.ps1`: the AMOLED's helper for the P4 (`--chip esp32p4`, bootloader
at 0x2000, finds the P4's COM port and keeps it in `p4\port.txt`, writes `ota_data_initial.bin` and `c6.bin` when they
are in `p4\firmware\`). From the cloud session, through the desktop app's shell on the PC (`p4/`):

- `flash.sh MD5 [SECS]`: checks `firmware/meteobus.bin`'s md5, stops a running log, flashes, logs (default 1800 s).
- `echo "<command>" > serial.send`: a console command; its output lands in `serial_live.txt`.
- `shots.sh OUT SCREEN...`: console `shot SCREEN 2` for each, then a copy of the log; `tools/shot.py LOG DIR` makes the
  PNGs. `shot panel 1` sends the frame buffer as shown (not a render), the only way to see what the panel really shows.
- `swipes.sh`: test swipes and the drag log lines.

Console commands added for this board: `shot`, `blitbench` (copy timings), `netstat` (lwIP counters: does anything from
outside reach the P4?), `c6update`. The others (`swipe`, `tap`, `page`, `screen`, `pictest`, `alert sample`, `key`...) are
the AMOLED's.

## Open

- **The phone can't open the settings page on the home network** (Chrome: ERR_TOO_MANY_RETRIES); the PC (wired) can,
  and the phone can on the display's own setup network. The AMOLED fails the same way, and during the phone's tries the
  display's counters didn't move: the router most likely keeps Wi-Fi devices apart (client / AP isolation, a guest
  network or band). Not checked yet in the router.
- **The settings page says "Could not reach the display"** when the display refuses a change for a missing key (401):
  the key is kept per address, so after the address changed (port 50443 to 443) the page had none. It should say to
  scan the QR code again (espforge's page).
- The alert screen's region map in landscape (no real alert yet; the sample has no map) and the update screen with a
  real update.
- `ui_snapshot()` (the console's `shot SCREEN`) once showed a one-row black line across the weather icon that the panel
  didn't have (strip render): a test tool's flaw to look at.
- Power: frequency scaling and a real screen-off (panel asleep) as on the AMOLED (v0.2.1).
- CI and releases for the P4 (component manifest, a second firmware in the flasher).
- ~~espforge: the patch's changes, the backlog entries~~: in espforge v0.6.0 (2026-10-31), taken by MeteoBus v0.3.2-rc.1.
  Run on the board the same day (the PC build above): boot clean (`diag: stats cost: task list -1 us, PSRAM largest
  block -1 us`), `audio: microphone ready (the ES8311's input)` from forge_audio, the microphone's level line kept
  moving across a test chime (-37 dB, -1.9 dB during the chime, -31 dB after: L212 holds on the ES8311_BOTH layout),
  the radar turned Wi-Fi power save off and on (`net: low latency on (1 users)` / `off`, `wifi status` ps=none / min),
  14 past frames in 4.8 s. One `Connection reset by peer` on the first GeoMet request right after power save went off,
  recovered on the retry: to watch. White flashes were not watched long enough to say.

## Lessons (2026-10-29, 30)

1. **Identify the board before porting.** "ESP32-P4-Function-EV-Board" was a guess; the board was a Guition
   JC1060P470C with a V2 panel. A black screen, then a white streaked band (the V1 table on a V2 panel), cost the first
   hours. Ask for the product link or a photo first.
2. **A board's pins from a working setup, not from its schematic or a guess.** The amplifier was on GPIO 11, not 20;
   the schematic's ES7210 isn't there (an I2C scan says what is). The software side looked fine (codec opened, sound
   "played") while nothing came out.
3. **On the P4, move pixels with the 2D-DMA**, not the CPU or the PPA's scaler (table above), and give the DSI's DMA
   the bus first.
4. **"Unreachable" from the network: count the packets on the board** (`netstat`) before changing ports or
   certificates. The C6's network split, then the router, were the causes; the display's server was never reached.
5. **LVGL calls DRAW_POST only when the band meets the object itself**, not its extra draw size (`lv_obj_redraw()`): a
   badge drawn past its object's edge loses the part in a band above or below the object. `LV_OBJ_FLAG_OVERFLOW_VISIBLE`
   makes the extra size count. `pictest` found it (7 rows) after a swipe; `shot SCREEN` (a whole render) never shows it.
6. **One codec, one device.** esp_codec_dev's `es8311_open()` rewrites the chip: share one IN_OUT device for speaker and
   microphone, created under a lock.
7. **esp_hosted's buffer pools are allocated before the scheduler**: their size can take the idle task's RAM (assert in
   `vApplicationGetIdleTaskMemory`, boot loop).
8. esptool won't write a C6 image to a P4's flash: pad it (4 KB) and find the image by its magic.
9. Python's `write_text()` on CRLF files (lesson 10 of CLAUDE.md) bit again here: check `file` on what you edit.
10. **A display fed from an interrupt every frame can't take long critical sections anywhere.** White flashes every
    few minutes were interrupts held off for 2-6 ms by statistics code (heap walks, the task list). Ask the user for
    the time of each flash and match it to the log (the once-a-minute statistics, one second before); then make the
    suspect run on demand at announced times while the user watches; time it (`walk`). Power, radio and bandwidth
    were plausible and wrong.

