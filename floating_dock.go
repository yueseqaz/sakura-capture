package main

import (
	"image"
	"time"

	"github.com/egoist/mygo"
	"github.com/egoist/mygo/ui"
)

const dockTitle = "Sakura · 悬浮工具栏"

func (a *application) ensureDock() {
	if a.control != nil {
		return
	}
	a.control = mygo.NewWindow(mygo.WindowOptions{
		Title: dockTitle, Width: 520, Height: 156, UseContentSize: true,
		Hidden: true, AlwaysOnTop: true, SkipTaskbar: true, DisableResize: true,
		DisableMaximize: true, TitleBarStyle: mygo.TitleBarHidden, HideWindowButtons: true,
		BackgroundColor: "#20262b", Content: ui.View(a.dockView),
	})
	a.control.OnClose(func(e *mygo.CloseEvent) {
		e.PreventDefault()
		if a.busy || a.rec != nil {
			a.stop()
		} else {
			a.collapseDock()
		}
	})
	// Idle screenshots also hide the dock before the engine freezes the desktop.
	// Recording rechecks affinity and refuses to start if exclusion is unsupported.
	_ = excludeWindow(a.control.NativeHandle())
	cfg := a.store.Config
	if cfg.DockSaved {
		moveDock(a.control.NativeHandle(), image.Pt(cfg.DockX, cfg.DockY))
	} else {
		_ = placeControl(a.control.NativeHandle(), image.Rect(0, 0, 1, 1))
	}
	a.dockEdge = cfg.DockEdge
	if a.dockEdge != "left" && a.dockEdge != "right" {
		a.dockEdge = ""
	}
}

func (a *application) hideDock() {
	if a.control != nil {
		a.control.Hide()
	}
}

func (a *application) sizeDock(w, h int) {
	if a.control == nil {
		return
	}
	a.dockAdjusting = true
	a.control.SetContentSize(w, h)
	s := readDock(a.control.NativeHandle())
	if !s.work.Empty() {
		x := s.bounds.Min.X
		if a.dockEdge == "left" {
			x = s.work.Min.X
		}
		if a.dockEdge == "right" {
			x = s.work.Max.X - s.bounds.Dx()
		}
		x = max(s.work.Min.X, min(x, s.work.Max.X-s.bounds.Dx()))
		y := max(s.work.Min.Y, min(s.bounds.Min.Y, s.work.Max.Y-s.bounds.Dy()))
		moveDock(a.control.NativeHandle(), image.Pt(x, y))
	}
	a.dockLast = readDock(a.control.NativeHandle()).bounds
	a.dockAdjusting = false
}

func (a *application) restoreDock() {
	if a.control == nil {
		return
	}
	a.dockCollapsed, a.dockPinned = false, false
	a.dockOutside = time.Now()
	a.sizeDock(520, 156)
	a.control.ShowInactive()
	a.control.Invalidate()
}

func (a *application) collapseDock() {
	if a.control == nil || a.busy || a.stopping {
		return
	}
	if a.dockEdge == "" {
		s := readDock(a.control.NativeHandle())
		a.dockEdge = "right"
		if s.bounds.Min.X-s.work.Min.X < s.work.Max.X-s.bounds.Max.X {
			a.dockEdge = "left"
		}
	}
	a.dockCollapsed, a.dockPinned = true, false
	a.sizeDock(44, 104)
	a.control.Invalidate()
}

func (a *application) expandDock(pin bool) {
	a.dockCollapsed, a.dockPinned = false, pin
	a.dockOutside = time.Now()
	h := 156
	if a.recording || a.rec != nil {
		h = 200
	}
	a.sizeDock(520, h)
	a.control.Invalidate()
}

type dockState struct {
	bounds, work image.Rectangle
	cursor       image.Point
	down         bool
}

func dockSide(bounds, work image.Rectangle, threshold int) string {
	if bounds.Min.X <= work.Min.X+threshold {
		return "left"
	}
	if bounds.Max.X >= work.Max.X-threshold {
		return "right"
	}
	return ""
}

func (a *application) dockTick() {
	if a.control == nil || !a.control.IsVisible() || a.dockAdjusting {
		return
	}
	s := readDock(a.control.NativeHandle())
	if s.work.Empty() {
		return
	}
	// Native coordinates are physical pixels, including negative monitor origins.
	if !s.down && !s.bounds.In(s.work) {
		a.sizeDockSize()
		s = readDock(a.control.NativeHandle())
	}
	if s.down {
		a.dockOutside = time.Now()
		return
	}
	if s.bounds != a.dockLast {
		a.dockEdge = dockSide(s.bounds, s.work, 24)
		a.dockPinned = false
		a.dockLast = s.bounds
		a.dockOutside = time.Now()
	}
	a.store.Config.DockX, a.store.Config.DockY = s.bounds.Min.X, s.bounds.Min.Y
	a.store.Config.DockSaved, a.store.Config.DockEdge = true, a.dockEdge
	if s.cursor.In(s.bounds) {
		a.dockOutside = time.Now()
		if a.dockCollapsed {
			a.expandDock(false)
		}
		return
	}
	if !a.dockCollapsed && !a.dockPinned && a.dockEdge != "" && !a.busy && !a.stopping && time.Since(a.dockOutside) > time.Second {
		a.collapseDock()
	}
}

func (a *application) sizeDockSize() {
	w, h := 520, 156
	if a.dockCollapsed {
		w, h = 44, 104
	} else if a.rec != nil || a.recording {
		h = 200
	}
	a.sizeDock(w, h)
}

func (a *application) dockView(c *ui.Context) {
	c.SetTheme(sakuraTheme(true))
	if a.dockCollapsed {
		ui.ButtonBase(c).Fill().Padding(4).Background(ink).Tooltip("展开 Sakura · 点击固定展开").OnClick(func() { a.expandDock(true) }).Children(func() {
			ui.Column(c).Fill().AlignItems(ui.Center).Justify(ui.Center).Gap(10).Children(func() {
				ui.Icon(c, shotIcon).Size(22, 22).TextColor(rose)
				label := "捕捉"
				if a.rec != nil || a.recording {
					label = "● REC"
					if a.paused {
						label = "暂停"
					}
				}
				ui.Text(c, label).FontSize(10).TextColor(ui.Hex("#f9f5f1"))
			})
		})
		return
	}
	if a.rec != nil || a.recording || (a.busy && a.cancelPreparation == nil) {
		a.controlView(c)
		return
	}
	ui.Column(c).Fill().Padding(14, 18).Gap(12).Background(ink).Children(func() {
		ui.Row(c).FillWidth().Gap(8).AlignItems(ui.Center).Children(func() {
			ui.Row(c).Grow(1).Gap(8).DragWindow().Children(func() {
				ui.Text(c, "SAKURA").Font("Bahnschrift").FontSize(13).Bold().TextColor(ui.Hex("#f2c4c5"))
				ui.Text(c, "随手捕捉 · 拖动贴边").FontSize(11).TextColor(ui.Hex("#aab5bc"))
			})
			ui.Button(c, "收纳").OnClick(a.collapseDock)
		})
		ui.Row(c).FillWidth().Gap(10).Children(func() {
			if a.cancelPreparation != nil {
				ui.Text(c, a.status).Grow(1).FontSize(20).Bold()
				actionButton(c, "取消录制", stopIcon, true).OnClick(a.stop)
			} else {
				actionButton(c, "截图", shotIcon, true).Grow(1).Height(40).OnClick(func() { a.screenshot("region") })
				actionButton(c, "录屏", videoIcon, false).Grow(1).Height(40).OnClick(func() { a.record("region") })
				ui.Button(c, "最近").Height(40).OnClick(func() { a.tab = 1; a.show() })
				ui.Button(c, "设置").Height(40).OnClick(func() { a.tab = 2; a.show() })
			}
		})
		text := a.status
		if text == "" {
			text = "截图 " + a.store.Config.ShotKey + "   /   录屏 " + a.store.Config.RecordKey
		}
		ui.Text(c, text).FillWidth().MaxLines(1).Ellipsis("…").FontSize(11).TextColor(ui.Hex("#aab5bc")).Tooltip(text)
	})
}
