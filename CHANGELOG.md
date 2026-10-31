# Changelog

What changed in each release. The display shows these notes on its update screen, and the settings page shows
them before you install. Only the entries newer than the version you have are shown. The web flasher site
publishes them as `notes.json`, built by `tools/make_flasher_site.py`.

How to write an entry: add a `## vX.Y.Z - YYYY-MM-DD` section at the top **before** tagging the release, with one
`- ` line per change. Write for the person holding the display, not for developers. Release candidates can get
their own `## vX.Y.Z-rc.N` section. Those are shown only to Beta users, while the final release's section should
list everything again.

## v0.3.2-rc.1 - 2026-10-31
- The shared software under the display (espforge) is at its v0.6.0, which now holds what this display had added on
  its own: the radar's faster downloads while it is open, one download at a time, the real screen off, the audio
  codecs, and the 7" display's needs. Nothing changes on the screen, on either display. For developers: no more
  `p4/espforge-p4.patch` or `forge_compat.c`; `forge_audio` is the sixth espforge component; the 7" display's firmware
  builds on the PC from the same manifest (its components under a target rule).
- Wide display (JC1060P470C): the divider between the two halves of the weather, stop and extras pages is now at the
  centre of the screen.
- Wide display: a new setting, "Forecast below, radar beside" (in Settings and on the settings page). With it on, the
  weather page shows the five days under the current conditions and the radar in the right half of the screen: tap
  the radar to play the last 3 hours, swipe down or up on it to zoom, tap the circle in its corner for the
  full-screen radar.

## v0.3.1 - 2026-10-30
- 7" display (Guition JC1060P470C): no more white flashes of the whole screen every few minutes. Its firmware is on
  the release page (jc1060p470c-v0.3.1.zip).
- The "Try it" demo on the install page works again. Nothing changes on the round display.

## v0.3.1-rc.1 - 2026-10-30
- 7" display (Guition JC1060P470C): no more white flashes of the whole screen every few minutes.
- The "Try it" demo on the install page works again. Nothing changes on the round display.

## v0.3.0 - 2026-10-29
- MeteoBus also runs on a 7" landscape display now, the Guition JC1060P470C: every screen laid out for its wide
  screen, five forecast days with their full names, and the settings page's QR code beside the settings. Its firmware
  is on the release page (jc1060p470c-v0.3.0.zip) and installs by USB for now. Nothing changes on the round display.

## v0.3.0-rc.1 - 2026-10-29
- MeteoBus also runs on a 7" landscape display now, the Guition JC1060P470C: every screen laid out for its wide
  screen, five forecast days with their full names, and the settings page's QR code beside the settings. Its firmware
  is installed by USB for now. Nothing changes on the round display.

## v0.2.1 - 2026-10-13
- The radar and bus map zooms are smoother (about 57 and 54 frames a second instead of 43).
- The radar's last 3 hours play as soon as you tap, even while the images are still loading: each one shows as soon as
  it arrives, instead of waiting for all of them.
- When the room has been quiet long enough to turn the screen off, it is now really off: the screen goes to sleep and
  the processor slows down, so the display uses less power at night. Your stop's departures refresh every 5 minutes
  meanwhile instead of every 30 seconds. A touch, a sound or picking it up wakes it as before.

## v0.2.1-rc.2 - 2026-10-13
- The radar's last 3 hours play as soon as you tap, even while the images are still loading: each one shows as soon as
  it arrives, instead of waiting for all of them.

## v0.2.1-rc.1 - 2026-10-11
- The radar and bus map zooms are smoother (about 57 and 54 frames a second instead of 43).
- When the room has been quiet long enough to turn the screen off, it is now really off: the screen goes to sleep and
  the processor slows down, so the display uses less power at night. Your stop's departures refresh every 5 minutes
  meanwhile instead of every 30 seconds. A touch, a sound or picking it up wakes it as before.

## v0.2.0 - 2026-10-10
- MeteoBus: the weather display and the RTC bus display in one. Everything the weather display (v1.15.0) does is
  here, with its own update site.
- Buses: swipe left from the weather for your RTC stops' next departures, one stop per page (drag up or down). The
  next bus in big, real time or scheduled, the three after it, and the route's notices in an orange pill (tap it).
- Add your stops on the phone's settings page, in the new "My stops" card: the stop number, the route, then pick the
  direction. Move them up or remove them there too.
- Tap a stop's route badge for the bus map: the stop, the route's path and its buses on the way. Swipe down or up to
  zoom, like the radar; swipe sideways to go back.
- The radar now opens with a tap on the weather icon (it has a small radar badge), and its last 3 hours load faster
  (3-4 s instead of 5-7). Going back is a sideways swipe everywhere: from the radar, the bus map and the alert screen.
- Weather alerts moved to the bottom of the weather screen, so the place name always shows. With an update waiting
  too, the alert pill has a blue dot, and the alert screen ends with "Update available".

## v0.2.0-rc.6 - 2026-10-10
- My stops (phone settings page): removing a stop right after moving one no longer removes the wrong stop; the buttons
  wait until the new order is saved.

## v0.2.0-rc.5 - 2026-10-10
- Bus map: the buses no longer cover the route's name, the next bus or the map credit at the top and bottom.

## v0.2.0-rc.4 - 2026-10-10
- The radar's last 3 hours load faster after it opens or zooms (3-4 s instead of 5-7): while the radar
  or the bus map is open, the Wi-Fi no longer naps between replies.

## v0.2.0-rc.3 - 2026-10-10
- The bus map needs less memory when it opens: the picture for the next zoom is only made when you first zoom.
- My stops (phone settings page): the list no longer redraws under your finger a few seconds after a change.

## v0.2.0-rc.2 - 2026-10-10
- Going back is a sideways swipe everywhere: from the radar, the bus map and the alert screen, which now slide in
  from the side. A tap on the bus map or an alert does nothing.
- The bus map zooms like the radar: the map grows or shrinks under your finger instead of going blank while the new
  one loads.
- The buses on the map are small bus icons, hidden while a zoom moves.
- The bus map's title and the OpenStreetMap credit no longer run past the round edge.

## v0.2.0-rc.1 - 2026-10-10
- Buses: swipe left from the weather for your RTC stops' next departures, one stop per page (drag up or down). The
  next bus in big, real time or scheduled, the three after it, and the route's notices in an orange pill (tap it).
- Add your stops on the phone's settings page, in the new "My stops" card: the stop number, the route, then pick the
  direction.
- Tap a stop's route badge for the bus map: the stop, the route's path and its buses on the way. Swipe down or up
  to zoom; swipe sideways to go back.
- The radar now opens with a tap on the weather icon (it has a small radar badge), and closes with a sideways swipe.
- Weather alerts moved to the bottom of the weather screen, so the place name always shows. With an update waiting
  too, the alert pill has a blue dot, and the alert screen ends with "Update available".

## v0.1.0-rc.2 - 2026-10-09
- The web flasher offers the Beta channel while there is no stable release yet.

## v0.1.0-rc.1 - 2026-10-09
- MeteoBus begins: the weather display (v1.15.0) under its new name, with its own update site. Bus departures from
  the RTC come next.
