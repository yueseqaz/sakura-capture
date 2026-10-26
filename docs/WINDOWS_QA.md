# Windows release acceptance

A green CI build is not interactive desktop certification. Complete this checklist on Windows before publishing a stable release.

- [ ] Clean Windows 11 x64: unzip, launch without admin, tray visible, no missing runtime.
- [ ] 100%, 125%, 150%, 200% scaling; two monitors with mixed DPI and negative coordinates.
- [ ] Region, screen and window-region snips; escape, reselect, every annotation, text/Chinese IME, undo/redo.
- [ ] Export PNG dimensions, colors and orientation; paste into Paint; verify redaction in exported pixels.
- [ ] Busy clipboard: PNG still saved, clipboard failure reported accurately.
- [ ] Silent, speaker loopback, microphone and mixed recordings; verify actual audible tracks.
- [ ] RGB frame orientation, cursor hotspot, control-window capture exclusion.
- [ ] 15/30/60 FPS and all bitrates; odd-sized region rounded down to even dimensions.
- [ ] Pause for 30 seconds and resume; no stale paused audio or gap in playback.
- [ ] One-hour 1080p30 recording: measure A/V drift, process-tree memory and CPU, no duration-proportional memory growth.
- [ ] Hotkey conflict, invalid shortcut, changed shortcut and tray actions while main window hidden.
- [ ] Stop while recording; close control window; attempt quit during setup/capture/encoding.
- [ ] No default audio device, denied microphone, unplugged headset, locked desktop, display disconnected.
- [ ] Read-only destination, nonexistent destination, full disk, Unicode path, long path.
- [ ] Verify partial-file and finalization errors are understandable; never claim crash recovery.
- [ ] Measure combined binary/zip size and idle resource use; record hardware and OS build.
- [ ] Check unsigned binary warnings without recommending disabling Windows security.
