# Third-party software

The Go module versions and checksums are pinned in go.mod and go.sum. This application uses:

- github.com/egoist/mygo — MIT — https://github.com/egoist/mygo
- github.com/ebitengine/purego — Apache-2.0 — https://github.com/ebitengine/purego
- github.com/go-text/typesetting — BSD-3-Clause / fonts may have separate licenses — https://github.com/go-text/typesetting
- golang.org/x/image, golang.org/x/net, golang.org/x/text — BSD-3-Clause — https://go.googlesource.com/
- Go runtime and standard library — BSD-3-Clause — https://go.dev/LICENSE

Native engine source is part of this repository and licensed under MIT. Windows SDK and operating-system media components are supplied by Microsoft. The engine links the MSVC runtime statically; its redistribution is governed by Microsoft's Visual Studio license.

No FFmpeg or external codecs are redistributed. System fonts are used; no third-party font files are bundled.

Full dependency licenses are copied from the pinned modules into the portable distribution by the build workflow.
