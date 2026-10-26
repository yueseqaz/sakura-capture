$ErrorActionPreference = 'Stop'
# Run from a Visual Studio x64 Native Tools PowerShell.
Set-Location (Split-Path $PSScriptRoot -Parent)
New-Item dist -ItemType Directory -Force | Out-Null
& cl /nologo /std:c++17 /EHsc /utf-8 /W4 /O2 /MT /DUNICODE /D_UNICODE native/main.cpp native/recorder.cpp native/snip.cpp /Fe:dist/sakura-engine.exe /link mfplat.lib mfreadwrite.lib mfuuid.lib ole32.lib uuid.lib user32.lib gdi32.lib windowscodecs.lib dwmapi.lib msimg32.lib
if ($LASTEXITCODE) { throw 'Native engine build failed' }
& ./dist/sakura-engine.exe selftest "$env:TEMP/sakura-selftest.mp4"
if ($LASTEXITCODE) { throw 'Media self-test failed' }
go test ./...
if ($LASTEXITCODE) { throw 'Go tests failed' }
Push-Location assets
& rc /nologo /fo app.res app.rc
if ($LASTEXITCODE) { throw 'Resource compile failed' }
& cvtres /nologo /machine:x64 /out:../rsrc_windows_amd64.syso app.res
if ($LASTEXITCODE) { throw 'Resource conversion failed' }
Pop-Location
$env:CGO_ENABLED = '0'
go build -trimpath -ldflags '-s -w -H=windowsgui' -o dist/SakuraCapture.exe .
if ($LASTEXITCODE) { throw 'Application build failed' }
