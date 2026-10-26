# Build verification — 2026-10-26

## v0.3.0 Candidate

Verified functional candidate: `b5234f03104d1f0d62021d231bab2b30ae80b80b`.

[Successful Windows verification workflow](https://github.com/yueseqaz/sakura-capture/actions/runs/40341243928)

- Go race tests and `go vet`, MSVC static-runtime build, real H.264/AAC encoding and decoding, and live recording command tests all passed.
- Actual Windows mouse interaction verified the persistent startup toolbar, the same toolbar HWND after recording, screenshot cancellation/restoration, recording pause/resume/stop acknowledgements, and graceful application exit.
- The real frameless window collapses to a 44-pixel-wide side tab at 100% DPI; Windows caption minimum sizing no longer prevents collapse. Expanded and collapsed recording controls both pass actual BitBlt pixel-exclusion checks over a known white background.
- Actual screenshot exports verify 3px/9px stroke widths, object movement/resizing, color-popover editing, copy without creating a file, and native Chinese text entry with double-click re-editing.
- Native image selftests verify grouped thickness/color undo, original-image eyedropper sampling, object deletion, crop undo and rendering. UI previews include 150% annotation controls.

The candidate ZIP measured 4,799,904 bytes (4.58 MiB), with 10,975,338 bytes (10.47 MiB) of extracted files. The final tagged package includes one additional color-popover preview, so its exact size and SHA-256 are reported with the release assets.

The startup smoke test observed a 38.32 MiB working set, 62.71 MiB private memory and 0.00% one-core-equivalent idle CPU over a two-second sample. These are observations on the hosted Windows runner, not sustained benchmarks or per-PC guarantees. No WinUI 3, .NET, Electron or FFmpeg runtime was added.

The runner uses Windows Server 2025, not a physical Windows 11 laptop. Audio switching includes synthetic sources and missing-endpoint recovery; physical microphone/speaker audibility, mixed-DPI multi-monitor interaction, long-session A/V drift and driver-specific exclusion still need the [hardware acceptance checklist](WINDOWS_QA.md). Hover expansion/automatic edge collapse is implemented, but this workflow's native interaction checks exercise manual collapse/expansion rather than certifying every pointer/monitor configuration.

## v0.2.2 Regression Coverage

New tests operate actual Windows HWNDs after building the distributed executables, rather than relying only on headless view rendering:

- Launch the screenshot engine with production flags and with legacy `HideWindow` flags; click a monitor, press Enter and decode the saved PNG.
- Drag a region, draw a rectangle, select it using V, change 3px to 9px and check the actual exported edge pixels; cancel and reselect without leaving the selector running.
- Launch the packaged GUI with isolated settings, click screen recording, complete selection and find the visible recording dock.
- Check always-on-top and `WDA_EXCLUDEFROMCAPTURE`, then BitBlt a pixel under the dock over a known white background. The captured pixel must be white, not dock content or a black placeholder.
- Click pause, resume and stop in the native dock; wait for actual engine acknowledgements and decode the resulting real MP4.
- Keep the synthetic 90-frame / six-audio-phase selftest separate from generic MP4 validation; real silent recordings must not be required to contain synthetic test tones.

The old launcher used `STARTF_USESHOWWINDOW/SW_HIDE`, which could suppress the engine's first `ShowWindow` call. Earlier render and startup tests did not cover that user-visible selection failure. The launcher now uses `CREATE_NO_WINDOW`; the native overlay also uses `SWP_SHOWWINDOW` defensively. Recording does not proceed if capture exclusion cannot be confirmed, including on pre-2004 Windows builds which can interpret exclusion as a black placeholder.

These hosted Windows interaction checks do not certify physical audio hardware, mixed-DPI displays or all display drivers. The physical acceptance checklist remains required before a stable release.

## v0.2.0

Verified functional candidate: `ecabf04`.

[Windows verification workflow](https://github.com/yueseqaz/sakura-capture/actions/runs/40317954293)

## Passed

- Go configuration, navigation, narrow-window layout and recording-dock interaction tests under the race detector, plus `go vet`.
- Audio command tests: acknowledgement-driven state, microphone enable/disable while paused, unavailable endpoints, failed requests, retryable stop, and unchanged next-session defaults.
- Native C++ engine compiled with MSVC x64 and the static C runtime.
- Actual H.264/AAC encoding and Media Foundation decoding: 90 video frames, 141 audio samples, monotonic timestamps and duration agreement.
- Six decoded audio phases checked for actual audibility/silence in both channels: silent / speaker / both / microphone / silent / speaker. Test-only tone endpoints exercise the same source switching and mixing path; this is not a claim of physical WASAPI hardware validation.
- Independent FFmpeg decoding on macOS confirmed RMS values of approximately 0 / 0.106 / 0.053+0.053 / 0.106 / 0 / 0.106 for those phases. FFmpeg is a development tool and is not distributed.
- Real Windows engine stdin controls: pause, enable/disable both sources while paused, resume, stop, and stop while paused. Missing devices report `AUDIO_ERROR` while video continues and saves an MP4.
- WIC PNG export and decode checked crop/corner pixels, baked-in mosaic, rectangle, ellipse, undo/redo; annotation-toolbar previews exported.
- Application icon and PerMonitorV2 DPI manifest embedded in the Go executable.
- Native GUI startup and second-instance handover verified on the GitHub-hosted Windows runner.
- Main interface, settings and recording/paused dock rendered on Windows and visually inspected.

## Observed sizes and idle sample

Verified v0.2.0 candidate (`ecabf04`) measured:

| Measurement | Result |
| --- | --- |
| SakuraCapture.exe | 9.92 MiB |
| sakura-engine.exe | 0.36 MiB |
| GUI working set | 53.85 MiB |
| GUI private memory | 69.60 MiB |
| Idle CPU, one-core equivalent | 0.00% during a 2-second sample |

These are startup observations on a hosted Windows runner, not a sustained benchmark or a guarantee for every PC. The capture engine is not running while the application is idle.

## Still requires real hardware acceptance

The synthetic audio test does not exercise a real speaker/microphone endpoint. The hosted runner had neither endpoint, so the live-control test verifies missing-device recovery rather than actual microphone sound. Windows Server is not equivalent to a user's Windows 11 laptop.

Mixed-DPI multi-monitor selection, actual audio audibility and levels, one-hour A/V drift, GPU/driver variations and capture-exclusion behavior need the separate [Windows acceptance checklist](WINDOWS_QA.md). The download remains a pre-release for those checks, not a UI-only prototype.
