# Changelog

What changed in each release. The display shows these notes on its update screen, and the settings page shows
them before you install. Only the entries newer than the version you have are shown. The web flasher site
publishes them as `notes.json`, built by `tools/make_flasher_site.py`.

How to write an entry: add a `## vX.Y.Z - YYYY-MM-DD` section at the top **before** tagging the release, with one
`- ` line per change. Write for the person holding the display, not for developers. Release candidates can get
their own `## vX.Y.Z-rc.N` section. Those are shown only to Beta users, while the final release's section should
list everything again.

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
