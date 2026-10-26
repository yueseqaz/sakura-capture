package main

import (
	"image"
	"testing"

	"github.com/egoist/mygo/ui"
	"github.com/yueseqaz/sakura-capture/internal/core"
)

func TestDockSide(t *testing.T) {
	work := image.Rect(-1920, 40, 0, 1080)
	for _, tc := range []struct {
		r    image.Rectangle
		want string
	}{
		{image.Rect(-1910, 100, -1390, 256), "left"},
		{image.Rect(-530, 100, -10, 256), "right"},
		{image.Rect(-1400, 100, -880, 256), ""},
	} {
		if got := dockSide(tc.r, work, 24); got != tc.want {
			t.Fatalf("%v: %q, want %q", tc.r, got, tc.want)
		}
	}
}

func TestFloatingDockViews(t *testing.T) {
	a := &application{store: core.Defaults()}
	tt := ui.NewTester(a.dockView, 520, 156)
	for _, text := range []string{"截图", "录屏", "最近", "设置", "收纳"} {
		r, ok := tt.Find(text)
		if !ok || r.X < 0 || r.Y < 0 || r.X+r.W > 520 || r.Y+r.H > 156 {
			t.Errorf("idle control %q overflows: %+v", text, r)
		}
	}
	a.dockCollapsed = true
	tt = ui.NewTester(a.dockView, 44, 104)
	if !tt.HasText("捕捉") {
		t.Fatal(tt.Texts())
	}
	a.recording = true
	tt = ui.NewTester(a.dockView, 44, 104)
	if !tt.HasText("● REC") {
		t.Fatal("collapsed recording must remain identifiable", tt.Texts())
	}
	a.paused = true
	tt = ui.NewTester(a.dockView, 44, 104)
	if !tt.HasText("暂停") {
		t.Fatal("paused recording must remain identifiable", tt.Texts())
	}
}
