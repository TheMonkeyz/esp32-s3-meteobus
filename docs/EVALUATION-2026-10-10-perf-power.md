# Evaluation, October 10, 2026: performance and power (v0.2.0)

**Results (October 11, v0.2.1, last test build v0.2.1-power.11): section 5.** The zooms went from 42-43 to 54-58 fps,
the hourly list already scrolls at 66 fps (the 52 below came from an old baseline reference), and "off" is now a real
off: the panel sleeps and the CPU idles at 80 MHz 72 % of the time (0 % before).

Goal set by the user: **60 fps minimum** on every move, and better power efficiency. Method: the graphify knowledge
graph (`graphify-out/`, updated for v0.2.0: 2450 nodes, 5190 edges) to find the code paths, then the code itself,
`sdkconfig.defaults`, the built `build/v55/sdkconfig`, `tools/harness/baseline.json` and `docs/DIAGNOSTICS.md` for
the numbers. Nothing was measured on the board in this session: every number below is the harness's reference
(`ref`) from the baseline or a documented measurement, with its date.

## 1. Frame rate: where v0.2.0 stands against 60 fps

The panel link allows it: QSPI at 80 MHz moves a full 466x466 RGB565 frame (434 KB) in ~11 ms (`display.c`,
`pclk_hz`), so ~90 full frames a second are possible. What runs below 60 is on the CPU side.

| Move | How it is drawn | Reference | 60 fps? |
|---|---|---|---|
| Screen, place and day drags | `slide.c` pictures, `drag_run()` | 65-69 fps, first frame after ~15 ms | yes |
| Page change from the phone | `slide_page()` | 65 fps | yes |
| Settings list scroll | `scroll_run()`, rows rendered + picture moved | 14.5 ms a frame (~69 fps); 52 fps with the flick's end | yes (borderline) |
| Hourly list scroll | same | 19.1 ms a frame (~52 fps); 36 fps with the flick's end | **no** |
| Radar zoom | `zoom_run()` / `zoom_fill()` raw frames | 42 fps (floor 28) | **no** |
| Bus map zoom | same code | 42.6 fps (floor 30) | **no** |
| Drag whose picture is stale | one LVGL render first | 78-130 ms before the first frame | **no** (one frame) |
| After a place switch | LVGL redraws the new place whole | 45 ms once (weather 45, with panel 69) | **no** (one frame) |
| Radar playback | `play_timer` 333 ms | 3 fps by design (frames 12 min apart) | not a target |
| Any full LVGL redraw | LVGL 9.2.2, one draw unit, no SIMD | weather 45 ms, hourly 35, radar 44 | **no**, known (lesson 21) |

So the drags the user feels most are at 60+. Three things are not:

### 1a. Zooms at 42 fps (radar and bus map)

`zoom_fill()` (`main/slide.c:1287`) does, per band and per frame: a nearest-neighbour gather of every pixel from the
PSRAM picture through `col[]`/`row[]` maps, an alpha blend of the overlay pixels, then `copy_swap()` over the whole
band again for the panel's byte order. Two passes over every pixel, and the gather reads PSRAM through the shared
cache while the SPI DMA sends the previous band. ~24 ms a frame; the bus alone needs 11.

Cheapest gains, in order:
1. **Fold the byte swap into the gather.** Keep the source picture already byte-swapped for the zoom's duration (one
   pass at the start, ~434 KB) so `zoom_fill()` writes final bytes. Removes one full pass per frame.
2. **Reuse repeated rows.** When `row[y] == row[y-1]` (zooming in, more than half the rows at scale 2x), `memcpy` the
   previous output row instead of gathering again; the overlay blend still runs per row.
3. If still short: gather 2 pixels per 32-bit store, or build one row of the band and let the DMA send while the
   next is built (already the case per band).
Target: 60 fps on `radar_zoom_fps` and `bus_map_zoom_fps`; raise their floors from 28/30 to 50 after.

### 1b. Hourly list scroll at 52 fps

Each frame renders the new rows with LVGL (~4-8 ms for the hourly rows: icons and a canvas), moves the picture
(434 KB memmove in PSRAM) and sends the whole list area (~8 ms of bus for 290 rows). Settings fits in 14.5 ms because
its rows are plain labels.

Options:
1. **Hardware vertical scroll of the panel** (MIPI DCS `VSCRDEF` 0x33 / `VSCSAD` 0x37: a fixed top area, a scrolling
   area, a fixed bottom area). The panel moves the rows itself; the firmware sends only the new rows. This is exactly
   the list layout (status row fixed, list between y 70 and 360). **Not verified on the CO5300**: it needs a throwaway
   build that sets 0x33/0x37 and checks the panel obeys (round panels sometimes ignore it). If it works, list scrolls
   drop to "render the new rows" (3-8 ms) and 60 fps is easy.
2. Without hardware scroll: render the hourly rows' icons once into the row strip cache (they are already pictures
   in `hr_draw`), and send only the rows that changed plus a moved region, as `scroll_move_fill` does. Smaller gain.

### 1c. The one slow frame after a switch or on a stale picture

The architecture already makes stale pictures rare (`slide_cache_dirty_rows`, idle re-rendering). What remains is
LVGL's full redraw of the new place after a switch (45 ms) while the picture on the panel is already right (the
flush hook keeps "picture equals panel"). An `lv_obj_invalidate` skipped when the cached picture is current would
remove that frame; check `pictest` after, as it is the check that would catch a wrong skip.

### 1d. Not worth doing (measured already)

- A newer LVGL or two draw units: `tools/lvglbench` shows 9.6.0 slower and two units no faster (October 4).
- Chasing LVGL's full-screen redraw under 16 ms: 35-45 ms with no hot spot (docs/TESTING.md §8). The picture
  pipeline is the right answer; extend it rather than fight LVGL.

## 2. Power: where v0.2.0 stands

Settings that matter (`sdkconfig.defaults`, `build/v55/sdkconfig`):

| Setting | Value | Effect |
|---|---|---|
| CPU | 240 MHz fixed, `CONFIG_PM_ENABLE` off | no frequency scaling, no automatic light sleep, ever |
| FreeRTOS tick | 1000 Hz | fine for the 15 ms LVGL loop |
| LVGL refresh / touch read | 15 ms (`LV_DEF_REFR_PERIOD`), `lvgl_task` sleeps 1-50 ms | the LVGL task wakes ~67 times a second even with nothing to draw |
| Wi-Fi | `WIFI_PS_MIN_MODEM`, off while the radar or bus map is open (`netq_awake`) | good: beacon-paced when idle, fast when it matters |
| Presence | mic + motion sampled every 100 ms, dim after 10 min, "off" after 60 min (`forge_presence`) | 2.8 % of core 0 at idle |
| Polling | weather 10 min; stop shown 30 s, other stops 5 min, buses 20 s, route notices 10 min; radar side work loop 1 s | reasonable for a mains-powered display |
| Idle CPU (DIAGNOSTICS) | core 0 4-6 %, core 1 1-2 % | the chip is mostly idle, at full speed |

The UI is black-background on an AMOLED: the display's own power is already low. Nothing is wasteful while the
screen is on. **The gap is when the screen is "off":**

- "Off" is brightness 0 (`display_brightness(0)`, command 0x51). The panel is never put to sleep (`SLPIN` 0x10 /
  `DISPOFF` 0x28 are not used after init), the CO5300 keeps refreshing its frame memory.
- Nothing in `main/` reads `presence_screen_off()`. While off, LVGL still renders the clock each minute and the
  status tick each second, touch is still polled every 15 ms, the shown stop is still fetched every 30 s, the radar
  task still runs its 1 s side-work loop, the CPU stays at 240 MHz.
- No battery today (`docs/IDEAS.md` lists battery level as an idea), so this costs mains power and heat only. It
  becomes the whole story the day the board runs on its battery connector.

### What to do, in order of return

1. **A real screen-off state** (forge_presence hook + `main.c`, so an espforge backlog item):
   - `set_brightness(0)` becomes "sleep": `SLPIN` + `DISPOFF`, and `lv_timer_enable(false)` (or a 200 ms
     `lvgl_task` sleep) so nothing renders; keep touch, mic and motion reads (they are the wake-up).
   - Wake: `SLPOUT` (needs ~120 ms), `lv_timer_enable(true)`, `slide_cache_dirty()` and one render, then brightness.
   - While off, treat the shown stop as hidden (5 min) and skip the radar's 1 s side-work wake. Weather stays at 10 min.
   - Harness: the `presence` suite already drives dim/off with short timers; add a check that the LVGL render count
     stops moving while off and resumes on wake.
2. **CPU frequency scaling only while off.** `CONFIG_PM_ENABLE` with `esp_pm_configure` (max 240, min 80 MHz, no
   light sleep) and an `ESP_PM_CPU_FREQ_MAX` lock held whenever the screen is on. Full speed for every frame the user
   sees, 80 MHz for the hours the screen is dark. Light sleep stays off: it would break the 15 ms touch/LVGL loop and
   the SPI DMA timing (lesson 21(a)).
3. **Leave alone:** Wi-Fi power save (already right), the polling intervals while on (RTC's 30 s is the point of a
   bus display), the presence sampling (2.8 %).

## 3. Verification plan (how to prove each change)

- Frame rate: `python tools/harness/harness.py perf navigation --expect <label>` and read `radar_zoom_fps`,
  `bus_map_zoom_fps`, `swipe_fps.scroll_hourly_list`, `scroll_frame_ms.scroll_hourly_list`; then the user's finger
  (lesson 21(h): synthetic drags pass while real ones miss). Raise the floors in `baseline.json` only after 3 runs.
- Pictures still equal the panel after any `slide.c` change: `pictest` in every language (`scroll_other_languages`).
- Power: no meter on the board. Use `diag: cpu` (core busy %) and `diag: tasks` while "off" with
  `POST /api/presence {"dim_s":10,"off_s":20}`, then post the original values back. A USB power meter on the cable
  is the only direct measurement; ask the user if one is at hand.

## 4. Graph notes (graphify)

- Updated incrementally for v0.2.0: 54 changed files, 6 extraction agents (docs and the four screenshots), AST for
  19 code files. 291 new nodes, 100 removed. Health: 3 self-loop edges, no dangling or missing endpoints.
- Communities that hold this topic: *Picture Pipeline Concepts*, *Slide Drag Engine*, *Touch Reading and Raw Runs*,
  *Display Driver and Scroll Steps*, *Download Queue (netq)*, *Tasks and Priorities Docs*, *Harness Perf Gates Docs*.
- `graphify-out/graph.html` to browse; `graphify query "<question>"` to ask.

## 5. What was done (October 10-11) and measured

Every number below is from the harness on the board (QIO flash), each build flashed and tested.

| Measure | v0.2.0 | v0.2.1-power | Change |
|---|---|---|---|
| Radar zoom (`radar_zoom_fps`) | 42-43 fps | 54-58 fps (57.8 in the final run) | `zoom_fill()`, overlay runs, 64-byte cache lines |
| Bus map zoom (`bus_map_zoom_fps`) | 42.6-43.5 fps | 53-55 fps (54.5) | same code |
| Hourly list scroll | 61.6 fps, 14.6 ms a frame | 66 fps, 12.8 ms | 64-byte cache lines |
| Settings scroll | 60.7 fps | 65.6 fps | same |
| Bus map first picture | 1725 ms | 1655 ms | same |
| CPU at 80 MHz, screen off (profiled, 60 s) | 0 % | 72 % | panel sleep + power management |
| Panel with the screen off | refreshing at brightness 0 | asleep (SLPIN) | `display_sleep()` |

**Zoom.** One pass per pixel (gather two pixels, swap to the panel's order in the same word), a row whose source row
is the one above is copied, and the overlays are runs of one colour and alpha blended with one multiply a pixel
(5-bit alpha, at most one colour step off; LVGL redraws the last frame exactly). Checked on the PC against the old code
at 110 scales: map pixels identical, overlay pixels within one step, AddressSanitizer clean. The zoom's log line now
splits a frame: fill (and the blend within it), bus wait, commands.

Still under 60 for the zooms: a frame is ~11 ms of fill (PSRAM reads) overlapping ~11 ms on the bus in two 32-row
buffers, plus ~1 ms of window commands and ~1 ms of touch and tables. Measured and **not** worth it: the column table in
internal RAM and an unrolled loop (185 vs 188 ms), holding the other core's downloads until the zoom ends (54 fps both
ways), 32-bit instead of 64-bit divisions in the tables (no change). Left to try: one window command per frame instead
of two per band (`RAMWRC` 0x3C or `CASET` once: needs a look at the panel, the harness can't see zoom frames), PSRAM at
120 MHz (experimental in ESP-IDF).

**Screen off.** `display_sleep()`: DISPOFF + SLPIN, and the "screen" power-management lock released, so an idle core
drops to 80 MHz (a running task still gets 240). Waking: SLPOUT from the presence task, then the LVGL task waits
120 ms, draws a whole frame and sends DISPON (the cross-core rule of lesson 21(a)). While off, the stop on view is
fetched every 5 min instead of 30 s, the bus map's buses not at all, and the radar task's idle round is 10 s instead of
1. The first touch or sound wakes it as before. Harness: `presence.dim_off_wake` checks the panel asleep, the lock
released, the panel and lock back after `wake`, and `pictest` after the wake; console `power`.

**Memory.** Internal RAM's low point: 21-31 KB across the runs (floor 25; v0.2.0-rc.6: 33), 25 in the final run
(19/19 passed, no performance problem). One run hit 21 (power management on); the same build again gave 31, a build
without power management 27-31. Place switch: 37-42 KB (floor 36). It sits at the floor: watch it in the next runs.

**Files.** `main/slide.c` (zoom), `main/display.c` / `display.h` (panel sleep, the lock, bus timing), `main/main.c`
(power management, the brightness hook), `main/departures.c` and `main/radar.c` (slower polling while off),
`main/console.c` (`power`), `sdkconfig.defaults` (`CONFIG_PM_ENABLE`, 64-byte cache lines), `tools/harness/suites.py`,
`web/emu/emu_display.c` and `web/emu/shim/esp_cpu.h` (the emulator still builds). espforge's `docs/BACKLOG.md` has
three entries.
