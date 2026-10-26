package main

import (
	"fmt"
	"strings"

	"github.com/egoist/mygo/ui"
)

func (a *application) toggleAudio(source string) {
	if a.rec == nil || !a.recording || a.stopping {
		return
	}
	on, pending := a.systemAudio, &a.systemPending
	if source == "mic" {
		on, pending = a.micAudio, &a.micPending
	} else if source != "system" {
		return
	}
	if *pending {
		return
	}
	state := "on"
	if on {
		state = "off"
	}
	if err := a.rec.send("audio " + source + " " + state); err != nil {
		a.audioNotice = "声音切换失败，请停止后重试。"
		return
	}
	*pending = true
	a.audioNotice = ""
}

// The engine acknowledges the real endpoint state; clicks never optimistically
// claim that a microphone is live when permission or initialization has failed.
func (a *application) audioEvent(event string) {
	f := strings.Fields(event)
	if len(f) < 3 || (f[0] != "AUDIO" && f[0] != "AUDIO_ERROR") {
		return
	}
	on, pending, name := &a.systemAudio, &a.systemPending, "电脑声音"
	if f[1] == "mic" {
		on, pending, name = &a.micAudio, &a.micPending, "麦克风"
	} else if f[1] != "system" {
		return
	}
	*pending = false
	*on = f[0] == "AUDIO" && f[2] == "on"
	if f[0] == "AUDIO_ERROR" {
		a.audioNotice = name + "不可用，请检查设备或权限；其他轨道继续录制。"
		a.status = a.audioNotice + " " + strings.Join(f[2:], " ")
	}
}

func (a *application) controlView(c *ui.Context) {
	c.SetTheme(sakuraTheme(true))
	ui.Column(c).Fill().Padding(16, 20).Gap(12).Background(ink).Children(func() {
		ui.Row(c).FillWidth().AlignItems(ui.Center).Gap(8).DragWindow().Children(func() {
			ui.Text(c, "SAKURA / LIVE").FontSize(11).Bold().TextColor(ui.Hex("#e9b3bb"))
			ui.Text(c, "拖动此处移动").Grow(1).FontSize(11).TextColor(ui.Hex("#9da9ae"))
			ui.Text(c, "MP4 · 本地保存").FontSize(11).TextColor(ui.Hex("#9da9ae"))
		})
		ui.Row(c).FillWidth().Gap(14).AlignItems(ui.Center).Children(func() {
			label, dot := "正在录制", rose
			if a.paused {
				label, dot = "已暂停", ui.Hex("#efbf77")
			}
			if a.stopping {
				label = "正在保存…"
			}
			ui.Column(c).Grow(1).Gap(2).Children(func() {
				ui.Text(c, fmt.Sprintf("%02d:%02d:%02d", a.elapsed/3600, a.elapsed/60%60, a.elapsed%60)).Font("Consolas").FontSize(32).Bold()
				ui.Text(c, "● "+label).FontSize(12).TextColor(dot)
			})
			p, icon := "暂停", pauseIcon
			if a.paused {
				p, icon = "继续", playIcon
			}
			actionButton(c, p, icon, false).Height(44).Disabled(a.stopping).OnClick(a.pause)
			actionButton(c, "停止并保存", stopIcon, true).Height(44).Disabled(a.stopping).OnClick(a.stop)
		})
		ui.Row(c).FillWidth().Gap(10).Children(func() {
			a.audioButton(c, "system", "电脑声音", speakerIcon, a.systemAudio, a.systemPending)
			a.audioButton(c, "mic", "麦克风", micIcon, a.micAudio, a.micPending)
		})
		notice := "声音开关仅影响后续内容 · 暂停时也可切换 · 停止 " + a.store.Config.StopKey
		if a.audioNotice != "" {
			notice = a.audioNotice
		}
		ui.Text(c, notice).FillWidth().FontSize(11).TextColor(ui.Hex("#bbc3c7"))
	})
}

func (a *application) audioButton(c *ui.Context, source, name string, icon *ui.SVG, on, pending bool) {
	state, fg, bg := "已关闭", ui.Hex("#aab5bc"), ui.Hex("#2b3339")
	if on {
		state, fg, bg = "已开启", ui.Hex("#b9e5d5"), ui.Hex("#2b423e")
	}
	if pending {
		state = "切换中…"
	}
	ui.ButtonBase(c).Grow(1).Height(43).Padding(12).Radius(10).Background(bg).TextColor(fg).
		Disabled(a.stopping || pending).Tooltip("切换本次录制的" + name + "，不会修改下次录制的默认设置").
		OnClick(func() { a.toggleAudio(source) }).Children(func() {
		ui.Row(c).FillWidth().Gap(8).AlignItems(ui.Center).Children(func() {
			ui.Icon(c, icon).Size(18, 18)
			ui.Text(c, name).Grow(1).FontSize(13)
			ui.Text(c, state).FontSize(11).Bold()
		})
	})
}
