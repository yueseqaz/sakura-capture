package main

import (
	"image/png"
	"os"
	"path/filepath"
	"testing"

	"github.com/egoist/mygo/ui"
	"github.com/yueseqaz/sakura-capture/internal/core"
)

func TestPreview(t *testing.T) {
	dir := os.Getenv("SAKURA_UI_PREVIEW")
	if dir == "" {
		t.Skip("set SAKURA_UI_PREVIEW to export native UI renders")
	}
	if err := os.MkdirAll(dir, 0755); err != nil {
		t.Fatal(err)
	}
	for tab, name := range []string{"capture", "history", "settings"} {
		a := &application{store: core.Defaults(), status: "随时捕捉，轻松记录。", tab: tab}
		a.store.Config.Directory = `C:\Users\You\Pictures\Sakura Capture`
		tt := ui.NewTester(a.view, 760, 800)
		f, e := os.Create(filepath.Join(dir, name+".png"))
		if e != nil {
			t.Fatal(e)
		}
		e = png.Encode(f, tt.Image())
		f.Close()
		if e != nil {
			t.Fatal(e)
		}
	}
	for _, paused := range []bool{false, true} {
		a := &application{store: core.Defaults(), recording: true, paused: paused, elapsed: 83, systemAudio: true}
		tt := ui.NewTester(a.controlView, 520, 200)
		name := "recording"
		if paused {
			name = "paused"
		}
		f, err := os.Create(filepath.Join(dir, name+".png"))
		if err != nil {
			t.Fatal(err)
		}
		err = png.Encode(f, tt.Image())
		f.Close()
		if err != nil {
			t.Fatal(err)
		}
	}
}
