# MeteoBus docs

For people using the display, start with the repository's [README](../README.md) (French: [guide.fr.md](guide.fr.md)).
The rest is for working on it. Most of it came with the weather display (esp32-s3-weather), whose versions are v1.x;
MeteoBus's are v0.x.

| File | What it is |
|---|---|
| [ARCHITECTURE.md](ARCHITECTURE.md) | How the firmware works and why: screens and gestures, tasks, display pipeline, moves (`slide.c`), weather, buses, network queue, radar, Wi-Fi setup, settings page and API, memory budget |
| [TESTING.md](TESTING.md) | How a change is built, flashed and proved: snapshots, the harness and its suites, settings page tests, host unit tests, profiling |
| [DIAGNOSTICS.md](DIAGNOSTICS.md) | The `diag:` log lines, how to measure memory, CPU and render times, and the reference findings |
| [HISTORY.md](HISTORY.md) | How the project grew (the weather display, then MeteoBus), how the work is done now, open threads |
| [IDEAS.md](IDEAS.md) | Feature backlog, bus ideas included, and what shipped |
| [MERGE-PLAN.md](MERGE-PLAN.md) | The plan for merging the weather and RTC bus displays (UI flow, page template, memory), and where the build differs |
| [MACOS.md](MACOS.md) | Working on a Mac: setup, the daily loop, the emulator, troubleshooting |
| [guide.fr.md](guide.fr.md) | The owner's guide in French |
| [translations/README.md](translations/README.md) | The Inuktitut draft: how it is made, generated and checked; [iu-review.md](translations/iu-review.md) is the reviewer's sheet |
| [EVALUATION-2026-10-10-perf-power.md](EVALUATION-2026-10-10-perf-power.md) | MeteoBus's frame rate and power review (October 10) and what v0.2.1 did about it: faster zooms, a real screen off |
| [EVALUATION-2026-10-02.md](EVALUATION-2026-10-02.md), [FIX-PLAN-2026-10-02.md](FIX-PLAN-2026-10-02.md) | The weather display's October 2-3 review and its fix plan (history; done) |

Elsewhere: [CLAUDE.md](../CLAUDE.md) (lessons and working notes for AI-assisted sessions),
[CHANGELOG.md](../CHANGELOG.md) (release notes, shown on the display), [web/emu/README.md](../web/emu/README.md)
(the display in the browser).
