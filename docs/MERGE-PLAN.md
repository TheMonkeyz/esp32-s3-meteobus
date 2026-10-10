# Merging weather_amoled and esp32-s3-rtcquebec: UI flow and memory

**Status (2026-10-10, v0.2.0-rc.3):** this file is the plan as agreed on 2026-10-09; it is kept as written below.
Built in v0.2.0: the row status | extras | weather | buses, the radar from the weather icon's badge, alerts in the
bottom pill (alert first, a blue dot when an update waits), the stop pages on the weather page's slots, the bus map
from the route badge, the radar and the bus map taking turns in PSRAM, the RTC's requests waiting for the other
downloads (`netq.c`). How it works now: docs/ARCHITECTURE.md, "Screens and gestures", "Buses", "Network queue".

**Where the build differs from this plan:**

- **Closing the maps:** the radar and the bus map close with a sideways swipe (in either direction; they slide in
  from the right) or after 5 min untouched, not with a tap or a swipe right. A tap on the radar plays the last 3 h; a
  tap on the bus map does nothing. The alert screen also closes with a sideways swipe and ignores taps but its
  "Update available >" row (the owner, 2026-10-10: back is a swipe everywhere).
- **No "Bus stops" row in Settings:** the stops are chosen on the phone's *My stops* card (`/api/favs`, `/api/route`).
  A long-press opens Settings on the weather screen and on the stop pages; it closes back to where it was opened.
- **The bus map's memory:** not 4 tiles plus a canvas (2.2 MB) but up to two 466×466 pictures of 434 KB, drawn from
  OSM tiles by the radar's `radar_osm_render()`: the one shown, allocated at the opening, and the next zoom's,
  allocated at the first zoom (both at the opening took PSRAM's low point to 112 KB, rc.2). Freed when the map closes.
  forge_map (espforge v0.5.0) was not taken: it keeps its pictures for good, and the plan wants them freed.
- **No `deps` task:** item 4 below put its 6 KB stack in PSRAM; a test build that did so (v0.2.0-rc.11, before rc.1
  was published) saw the task stop for good in the middle of a request, and on an internal stack internal RAM failed
  its floors. There is no `deps` or bus map task: the radar task runs the buses' work in its idle time
  (`radar_set_side_work`: the bus map's picture, else one RTC request, `deps_step()`).
- **One download at a time, partly:** `netq.c` keeps the RTC's requests out of the weather loop's and the radar's
  downloads; the weather loop and the radar don't wait for each other (as before the merge). Since v0.2.0-rc.4 it also
  turns Wi-Fi power save off while a map is open.
- **Bus notices:** the pill shows the first notice's title or "N notices" (the plan's "Detour: route 800" wording was
  an example).
- **espforge versions** (item "Foundation choice"): written 2026-10-09; on 2026-10-10 `main/idf_component.yml` pins
  espforge v0.3.0 for all five components.

## Context
The user asked how the UI would work if the two apps for the Waveshare ESP32-S3-Touch-AMOLED-1.75 were merged, and whether
the board has enough memory for both. This is a design answer, with no code changes yet.

## UI flow (agreed with the user)
```
status | extras | WEATHER (↕ places) | BUSES (↕ stops)          ← left/right swipes
             tap map icon → radar      tap map icon → bus map  ← each a screen of its own, closed by tap / swipe right
```
- The radar leaves the row. It opens from a **map icon on the weather screen**. The bus map opens from the same icon in
  the same place on the stop screen. Both maps zoom the same way: down zooms in, up zooms out. Both close by a tap or a
  swipe right, and on their own after 5 min idle.
- **Icon placement (the user chose option 3):** a badge sits on the hero icon. Tap the weather icon (radar badge) or the
  route badge (map-pin badge) to open the map.
  - The badge is a 30 px circle, outlined in `C_ACCENT`, at the hero icon's top-right corner (about x 186, y 118).
  - The touch zone covers the whole hero icon plus the badge, about 96×96 px. It is checked before the y < 200 alert tap
    zone. Taps on the hero's temperature or minutes keep their current meaning.
  - The badge and the icon are not clickable themselves (lessons 7 and 18). The zone does the work. The glyphs are drawn
    as shapes, like `drop_draw` / `wind_draw`, since the font has no symbols.
  - On the weather screen today, a tap at y < 200 opens the alert screen only while an alert is active. The hero zone
    has to be carved out of that area.
- **Alerts, both screens (the user chose B):** a pill at the bottom, at y ≈ 404–434, where weather's update pill is
  today. The place or stop name stays at y 72.
  - On weather, a tap at y < 200 no longer opens the alert screen. The pill's own touch zone does, plus about 10 px
    around it.
  - The forecast and departure columns move up about 8 px (icons at y 350, values at y 376), so the pill clears them.
  - **The bottom slot is shared:** alert first, update second. With no alert, the update pill shows as it does today.
    **With both (the user's choice):** the alert pill gets a small `C_ACCENT` dot (about 8 px) at its right end. A tap
    opens the alert screen, which ends with an "Update available >" row; that row opens the update screen and closes
    back to the alert screen. Settings → Updates and the status screen still show the update as today.
  - Showing or hiding the dot marks only the pill's rows (`slide_cache_dirty_rows`), not the whole picture (lesson 21j).
  - The round edge limits the width to about 210 px (`LV_LABEL_LONG_DOT`). Bus texts are kept short: "Detour: route
    800", or "3 notices" when there are several. The full text is on the alert screen, which is the same screen for
    weather and buses. This drops the separate bus alerts page.
  - Weather's pill style is kept: radius 15, `f_tiny`, coloured by level (yellow, orange or red). Bus pills are orange.
  - It sits just above the row dots (y 447). The snapshot check makes sure they don't touch.
- **Long press** opens Settings everywhere: one Settings screen with a "Bus stops" row.

### Harmonized page template (both screens share the same slots)
| Slot | y | Weather | Bus stop |
|---|---|---|---|
| status | 16 | "Updated 3 min ago" | "Updated 12:44" / "Can't reach the RTC" (was y 360) |
| clock | 38 | clock | clock (was y 22, smaller font) |
| title | 72 | place name (always shown) | stop name · number (was y 134) |
| hero | 112 | weather icon + temperature (f_big 96) | route badge 80×80 + minutes (f_big) + "min" (was y 56 and 178) |
| subtitle | 214 | condition | → direction (was y 106) |
| detail | 248 | feels · humidity · wind | real time / scheduled (was y 256) |
| divider | 298 | — | — |
| 3 columns | 306 / 350 / 376 | day / icon / high-low → hourly | "in N min" / time / kind (was a single row at y 300) |
| bottom pill | 404–434 | alert (by level), else update | bus notice (orange), else update |
| right-edge dots | — | places | stops |
| map badge | on hero icon | radar | bus map |

- Both screens use weather's fonts and colours (`f_time`, `f_city`, `f_big`, `f_cond`, `f_small`, `f_tiny`;
  `C_TEXT` / `C_DIM` / `C_ACCENT`).
- Tapping a column opens details in both: the hourly view for weather, and could be the full departure list for buses.
- To check in snapshots: long direction names at y 214 in French and Inuktitut, and that the badge doesn't touch the
  temperature or minutes text.

## Memory
| Resource | weather alone | rtc alone | Merged |
|---|---|---|---|
| Flash, app slot 3 MB | 2.00 MiB | 1.80 MiB | Estimated 2.3 to 2.6 MiB. Both include the same IDF, Wi-Fi, TLS, LVGL and espforge code. Fits, with less headroom. |
| Flash, other | 4 MB mapcache | none | Fits. Weather's layout leaves about 5.4 MB unused. |
| PSRAM, 8 MB | Low point 452 KB free (floor 300) | Low point 4.8 MB free | Does **not** fit as is. Fits only with sharing, see below. |
| Internal RAM | Low point 38 KB (floor 25), place switch 44 (floor 36) | Low point 79 KB | **The tightest.** Each TLS download costs 10 to 15 KB of internal RAM. |

What the merge needs to fit:
1. **One picture cache:** a single slide.c with its 5 slots, not two (about 2.2 to 2.6 MB saved).
2. **The radar and the bus map take turns:** the radar's 15 frames (3.3 MB), its basemap and its composed screen (0.87 MB)
   versus the bus map's 4 tiles plus its canvas (2.2 MB). Each is allocated when its screen opens and freed when it closes.
   The cost: the radar reloads after the bus map has been opened. Opening the maps by tap makes this natural: only one
   can be open at a time, and neither is a neighbour in the row, so slide.c never caches a picture of it.
3. **One download at a time:** a single network queue shared by the weather, radar, alerts and RTC fetches. Then the
   internal RAM low point stays where it is today, with no added TLS session.
4. **Smaller additions:** the `deps` task stack (6 KB) goes in PSRAM, as `radar_dec`'s does. RTC's JSON buffers are
   allocated per request (128 KB of notices) and freed after each one.

## Foundation choice (if we go ahead)
weather_amoled uses espforge v0.3.0 with its own `slide.c` and `pager.c`. rtc uses v0.5.0-rc.1 with forge_lvgl, forge_map
and forge_settings. Recommended: start from weather_amoled, the more mature and more tuned app, and port rtc's
`departures.c` and its stop page and map into it.

## New project and repo: esp32-s3-meteobus
These steps are outward-facing. I'll confirm with the user before running the GitHub ones.

1. **Folder** `C:\Users\lmathieu\ESPDEV\esp32-s3-meteobus`.
   - Copy the weather_amoled tree. Leave out `build*/`, `managed_components/`, `dependencies.lock`, `.espforge/`, the
     logs and flash files (`serial_*.txt`, `flash_*`, `*.status`, `*.done`), `harness.ask` and `.git`.
   - Then `git init -b main`. History starts fresh; the README links the two repos the project came from.
2. **Renames:** app "MeteoBus", the setup network "MeteoBus-Setup", the certificate name, the User-Agent, and the update
   site `themonkeyz.github.io/esp32-s3-meteobus/`.
   - These live in the `CONFIG_FORGE_*` values in `sdkconfig.defaults`, `CMakeLists.txt` (project name), the i18n app
     name, `tools/make_flasher_site.py`, the harness config, the flasher site, README and CLAUDE.md.
   - Reset CHANGELOG to `v0.1.0`, `tools/harness/baseline.json` (it is measured again on the merged app), and
     `version.txt`.
   - Use rtcquebec's `new-project` skill as the checklist, adapted to a weather_amoled base.
3. **GitHub repo** `TheMonkeyz/esp32-s3-meteobus`, public, created with `gh repo create`, then push `main`. Same settings
   as `esp32-s3-weather` (read today):
   - Ruleset **"protect main"** (`~DEFAULT_BRANCH`): no deletion, no force push, a PR with 1 approval, stale reviews
     dismissed on push, extra approval for unattributed changes, merge/squash/rebase allowed. Admin role (id 5) bypasses
     always.
   - Ruleset **"release tags"** (`refs/tags/v*`): no deletion, force push, creation or update. Admin bypasses always.
   - **Pages**: `build_type: workflow`. The `github-pages` environment allows deployments from branch `main` and tags
     `v*`.
   - **Actions**: enabled, all actions allowed, default workflow permissions `read`, cannot approve PRs. There are no
     secrets or variables to copy.
   - Repo flags as on weather: issues, wiki and projects on; all three merge methods on; no auto-merge.
   - **Workflows:** `.github/workflows/firmware.yml` and `macos.yml`, with the names and site changed.
   - Rulesets are created with `gh api repos/TheMonkeyz/esp32-s3-meteobus/rulesets -X POST --input <json>`, from JSON
     exported from weather's rulesets with ids removed.
   - Then `gh api` again to check that both repos' rulesets, Pages and environment policies match.
4. **First release:** a `v0.1.0-rc.1` built from the unchanged weather base, under the new name. It proves CI, Pages and
   OTA on the new site before any merge work.
5. **espforge backlog:** add an entry to `espforge\docs\BACKLOG.md` for a script that sets up GitHub (the rulesets,
   Pages, environment) for new projects, since this is the third time.

## Verification (when implemented)
- `idf.py size` against the 3 MB slot.
- Harness gates: `psram_min_kb` (floor 300), `internal_min_kb` and `.place_switch`, `failed_allocs` 0.
- Run them with an active alert, the radar open, then the bus map open.
- Snapshot the new screens in English, French and Inuktitut.
