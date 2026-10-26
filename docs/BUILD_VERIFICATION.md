# Build verification — 2026-10-26

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
