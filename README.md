# Sakura Capture

A small, offline Windows screenshot and screen-recording application. Native Go / [MyGo](https://mygo.egoist.dev/) interface, with a native C++ Windows capture engine. No Electron, WebView, FFmpeg, cloud service or account.

**中文使用说明：[docs/USER_GUIDE.zh-CN.md](docs/USER_GUIDE.zh-CN.md)**

## Download

1. Open [Windows Build](https://github.com/yueseqaz/sakura-capture/actions/workflows/windows.yml).
2. Select the latest **successful** build and download `SakuraCapture-windows-x64` under Artifacts (GitHub login required).
3. Extract the artifact, then fully extract `SakuraCapture-windows-x64.zip`.
4. Keep `SakuraCapture.exe` and `sakura-engine.exe` in the same folder. Run `SakuraCapture.exe`.

Tagged versions create a **draft** GitHub Release for manual verification before publication. Builds are currently unsigned: Windows SmartScreen may warn. Verify the source and SHA-256 before running; do not disable Windows security globally.

## Included

- Region, monitor and visible-window-region screenshots, saved as PNG and copied to the clipboard.
- Screenshot rectangles, arrows, freehand drawing, text, destructive mosaic, five colors, three widths and undo/redo.
- Region, monitor and visible-window-region recording to H.264 / AAC MP4.
- Default system audio (WASAPI loopback), default microphone, mixed audio, or silent recording.
- 15 / 30 / 60 FPS, bitrate presets, mouse cursor capture, optional countdown, pause/resume and graceful stop.
- Native MyGo interface, tray, configurable global shortcuts, local history and persistent settings.
- Windows-native engine built with the static MSVC runtime; no additional browser or .NET runtime.

## Requirements and honest boundaries

- Windows 11 x64 recommended. Windows 10 22H2 uses the same APIs but needs manual compatibility validation. ARM64 is not currently packaged.
- Windows N/KN requires Media Feature Pack. Microphone recording needs Windows microphone permission and a default input device. System audio needs a default render endpoint.
- Capture uses **GDI screen capture**, not Windows Graphics Capture. Window mode records a fixed visible rectangle: it does not track window movement or capture hidden/minimized content.
- Monitor recording supports one monitor per capture. A manually selected region can span monitors; media dimensions are limited to 7680x4320 and rounded down to even pixels for H.264.
- Audio endpoints are chosen when recording starts; changing devices during recording is not seamless. Device errors stop recording with a message.
- Pause/resume is supported. Locked desktops, UAC secure desktop, protected video, exclusive fullscreen and some hardware overlays are not supported capture targets.
- MP4 is finalized on normal stop. Forced termination, power loss or storage failure may leave an unplayable file. Recovery is not guaranteed.
- Capture-control exclusion uses Windows display affinity; its behavior still needs validation on target Windows machines and display drivers.
- 60 FPS and high-resolution performance depend on the machine. Package size, idle memory and real-time performance must be measured, not inferred from framework marketing.
- No OCR, scrolling screenshots, GIF, webcam, video editing, auto-start or auto-updater in this version. These are not represented as implemented features.

## Development

UI: Go 1.27.3+, MyGo v0.6.4 (pinned). Engine: Visual Studio 2022+ C++ tools and Windows SDK, C++17. Windows SDK libraries supply Media Foundation, WASAPI, GDI and WIC.

```sh
go test ./...
GOOS=windows GOARCH=amd64 CGO_ENABLED=0 go build -o SakuraCapture.exe .
```

The cross-compiled executable alone is **not** a complete distribution: build the C++ engine on Windows with `scripts/build-windows.ps1`, or use GitHub Actions. The UI can run on macOS for layout development, but capture actions deliberately report that Windows is required.

The workflow tests Go configuration/state and headless native UI, builds both binaries, and actually encodes **and decodes** a synthetic H.264/AAC recording. It does not claim to test interactive multi-monitor selection or real audio hardware on hosted runners. See [Windows acceptance checklist](docs/WINDOWS_QA.md).

## Architecture

- `main.go`: native MyGo application and workflow coordination.
- `internal/core`: configuration, history, output naming and unit tests.
- `native/snip.cpp`: DPI-aware selection and GDI screenshot editor; WIC PNG and CF_DIB clipboard.
- `native/recorder.cpp`: GDI frame capture, WASAPI shared capture, stereo mix, H.264/AAC sink writer, pause/resume and media smoke test.
- `native/main.cpp`: engine protocol, startup, PNG encoding.
- `.github/workflows/windows.yml`: Windows builds, tests, checksums and portable artifacts.

The child engine receives paths and settings as argument arrays (no shell interpolation). Recording control uses standard input, not a local network port. Screenshots/recordings stay in the user-selected directory; settings and the last 80 history entries stay in `%APPDATA%/SakuraCapture/settings.json`. No analytics or automatic uploads.

## License

MIT. See [third-party notices](THIRD_PARTY_NOTICES.md).
