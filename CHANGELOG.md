# Changelog

What changed in each release. The display shows these notes on its update screen, and the settings page shows
them before you install. Only the entries newer than the version you have are shown. The web flasher site
publishes them as `notes.json`, built by `tools/make_flasher_site.py`.

How to write an entry: add a `## vX.Y.Z - YYYY-MM-DD` section at the top **before** tagging the release, with one
`- ` line per change. Write for the person holding the display, not for developers. Release candidates can get
their own `## vX.Y.Z-rc.N` section. Those are shown only to Beta users, while the final release's section should
list everything again.

## v0.1.0-rc.2 - 2026-10-09
- The web flasher offers the Beta channel while there is no stable release yet.

## v0.1.0-rc.1 - 2026-10-09
- MeteoBus begins: the weather display (v1.15.0) under its new name, with its own update site. Bus departures from
  the RTC come next.
