# Changelog

What changed in each release. The display shows these notes on its update screen, and the settings page shows
them before you install. Only the entries newer than the version you have are shown. The web flasher site
publishes them as `notes.json`, built by `tools/make_flasher_site.py`.

How to write an entry: add a `## vX.Y.Z - YYYY-MM-DD` section at the top **before** tagging the release, with one
`- ` line per change. Write for the person holding the display, not for developers. Release candidates can get
their own `## vX.Y.Z-rc.N` section. Those are shown only to Beta users, while the final release's section should
list everything again.

## v0.2.0-rc.1 - 2026-10-10
- Buses: swipe left from the weather for your RTC stops' next departures, one stop per page (drag up or down). The
  next bus in big, real time or scheduled, the three after it, and the route's notices in an orange pill (tap it).
- Add your stops on the phone's settings page, in the new "My stops" card: the stop number, the route, then pick the
  direction.
- Tap a stop's route badge for the bus map: the stop, the route's path and its buses on the way. Swipe down or up
  to zoom; tap or swipe sideways to close.
- The radar now opens with a tap on the weather icon (it has a small radar badge), and closes with a sideways swipe.
- Weather alerts moved to the bottom of the weather screen, so the place name always shows. With an update waiting
  too, the alert pill has a blue dot, and the alert screen ends with "Update available".

## v0.1.0-rc.2 - 2026-10-09
- The web flasher offers the Beta channel while there is no stable release yet.

## v0.1.0-rc.1 - 2026-10-09
- MeteoBus begins: the weather display (v1.15.0) under its new name, with its own update site. Bus departures from
  the RTC come next.
