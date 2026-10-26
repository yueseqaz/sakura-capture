package main

import (
	"testing"

	"github.com/egoist/mygo/ui"
	"github.com/yueseqaz/sakura-capture/internal/core"
)

func TestCaptureView(t *testing.T) {
	a := &application{store: core.Defaults(), status: "就绪"}
	tt := ui.NewTester(a.view, 760, 740)
	for _, s := range []string{"区域截图", "区域录屏", "屏幕截图", "原生桌面 · 本地保存 · 无需登录"} {
		if !tt.HasText(s) {
			t.Errorf("missing %s; texts=%q", s, tt.Texts())
		}
	}
}
func TestSettingsView(t *testing.T) {
	a := &application{store: core.Defaults(), tab: 2}
	tt := ui.NewTester(a.view, 760, 1200)
	if !tt.HasText("全局快捷键") {
		t.Fatal(tt.Texts())
	}
}
func TestHistoryEmpty(t *testing.T) {
	a := &application{store: core.Defaults(), tab: 1}
	tt := ui.NewTester(a.view, 760, 740)
	if !tt.HasText("还没有捕捉记录") {
		t.Fatal(tt.Texts())
	}
}
func TestTrayPNG(t *testing.T) {
	if len(trayIcon()) < 40 {
		t.Fatal("missing tray image")
	}
}

func TestNavigation(t *testing.T) {
	a := &application{store: core.Defaults()}
	tt := ui.NewTester(a.view, 760, 800)
	if e := tt.Click("最近文件"); e != nil {
		t.Fatal(e)
	}
	if a.tab != 1 || !tt.HasText("还没有捕捉记录") {
		t.Fatal("history navigation failed")
	}
	if e := tt.Click("设置"); e != nil {
		t.Fatal(e)
	}
	if a.tab != 2 || !tt.HasText("全局快捷键") {
		t.Fatal("settings navigation failed")
	}
	if e := tt.Click("捕捉"); e != nil {
		t.Fatal(e)
	}
	if a.tab != 0 || !tt.HasText("区域录屏") {
		t.Fatal("capture navigation failed")
	}
}
