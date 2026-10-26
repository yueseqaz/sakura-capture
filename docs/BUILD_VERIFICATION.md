# Build verification — 2026-10-26

Verified source: `611baa6`.

[Successful Windows workflow](https://github.com/yueseqaz/sakura-capture/actions/runs/40312390713)

## Passed

- Go unit tests and native UI tests under the race detector, plus `go vet`.
- Native C++ engine compiled with MSVC x64, static C runtime.
- Actual H.264/AAC encoding and Media Foundation decoding: 90 decoded video frames, 141 decoded audio samples, monotonic timestamps and duration agreement.
- Synthetic screenshot exported and decoded through WIC: crop size and corner pixels, baked-in mosaic, rectangle annotation, undo/redo.
- Application icon and PerMonitorV2 DPI manifest embedded into the Go Windows executable.
- Native GUI launched successfully on the GitHub-hosted Windows runner; second instance exited successfully after handing over to the first.
- Native UI renders exported on Windows and visually inspected; synthetic recording downloaded to macOS and independently inspected with FFmpeg/FFprobe, including frame orientation. FFmpeg is a development verification tool only and is not distributed with the application.

## Observed sizes and idle sample

| Measurement | Result |
| --- | --- |
| SakuraCapture.exe | 9.89 MiB |
| sakura-engine.exe | 0.34 MiB |
| GUI working set | 53.06 MiB |
| GUI private memory | 68.18 MiB |
| Idle CPU, one-core equivalent | 0.00% during a 2-second sample |

These are startup observations on a hosted Windows runner, not a sustained benchmark or a guarantee for every PC. The engine is not running while the application is idle.

## Still requires real hardware acceptance

The synthetic test does not exercise a real speaker/microphone endpoint, and a hosted Windows Server runner is not equivalent to a user's Windows 11 laptop. Mixed-DPI multi-monitor selection, actual audio audibility, one-hour A/V drift, GPU/driver variations and capture-exclusion behavior need the separate [Windows acceptance checklist](WINDOWS_QA.md).

This first downloadable release is marked pre-release for those remaining hardware checks; it includes functioning screenshot/editor/recording workflows rather than a UI-only prototype.
