package main

import (
	"bytes"
	"fmt"
	"image"
	"image/color"
	"image/png"
	"log"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"github.com/egoist/mygo"
	"github.com/egoist/mygo/ui"
	"github.com/yueseqaz/sakura-capture/internal/core"
)

var version = "dev"

type application struct {
	store                                       core.Store
	configPath, status                          string
	win, control                                *mygo.Window
	tray                                        *mygo.Tray
	tab                                         int
	busy, recording, paused, stopping, quitting bool
	rec                                         *engine
	started                                     time.Time
	elapsed                                     int
	registered                                  []string
}

func main() {
	root, e := os.UserConfigDir()
	if e != nil {
		log.Fatal(e)
	}
	a := &application{configPath: filepath.Join(root, "SakuraCapture", "settings.json"), status: "随时捕捉，轻松记录。"}
	a.store, e = core.Load(a.configPath)
	if e != nil {
		a.status = e.Error()
	}
	mygo.App.WhenReady(a.ready)
	if e = mygo.App.Run(); e != nil {
		log.Fatal(e)
	}
}
func (a *application) dispatch(fn func()) {
	mygo.App.WhenReady(func() {
		fn()
		if a.win != nil {
			a.win.Invalidate()
		}
		if a.control != nil {
			a.control.Invalidate()
		}
	})
}
func (a *application) save() {
	if e := a.store.Save(a.configPath); e != nil {
		a.status = "保存设置失败: " + e.Error()
	}
}
func (a *application) ready() {
	a.win = mygo.NewWindow(mygo.WindowOptions{Title: "Sakura Capture", Width: 760, Height: 740, MinWidth: 640, MinHeight: 620, Content: ui.View(a.view)})
	a.win.OnClose(func(e *mygo.CloseEvent) {
		if !a.quitting && a.tray != nil {
			e.PreventDefault()
			a.save()
			a.win.Hide()
		}
	})
	a.tray, _ = mygo.NewTray(mygo.TrayOptions{ID: "sakura-capture", Icon: trayIcon(), ToolTip: "Sakura Capture"})
	if a.tray != nil {
		a.tray.SetMenu(mygo.NewMenu([]*mygo.MenuItem{
			{Label: "打开 Sakura Capture", Click: menuClick(a.show)},
			{Label: "截图", Click: menuClick(func() { a.screenshot("region") })},
			{Label: "区域录屏", Click: menuClick(func() { a.record("region") })},
			{Label: "停止录屏", Click: menuClick(a.stop)},
			{Type: mygo.MenuItemSeparator},
			{Label: "退出", Click: menuClick(a.quit)},
		}))
		a.tray.OnClick(a.show)
	}
	a.bindKeys()
	mygo.App.OnBeforeQuit(func(e *mygo.QuitEvent) {
		if a.busy || a.rec != nil {
			e.PreventDefault()
			a.status = "请先完成截图或停止录制，再退出。"
			a.show()
		} else {
			a.save()
		}
	})
}
func menuClick(fn func()) func(*mygo.MenuItem, *mygo.Window) {
	return func(*mygo.MenuItem, *mygo.Window) { fn() }
}
func trayIcon() []byte {
	im := image.NewNRGBA(image.Rect(0, 0, 32, 32))
	for y := 3; y < 29; y++ {
		for x := 3; x < 29; x++ {
			if x < 7 || x > 24 || y < 7 || y > 24 || ((x-16)*(x-16)+(y-16)*(y-16) < 30) {
				im.SetNRGBA(x, y, color.NRGBA{R: 224, G: 94, B: 114, A: 255})
			}
		}
	}
	var b bytes.Buffer
	png.Encode(&b, im)
	return b.Bytes()
}
func (a *application) show() { a.win.Show(); a.win.Focus(); a.win.Invalidate() }
func (a *application) quit() {
	if a.busy || a.rec != nil {
		a.status = "请先结束当前截图或录制。"
		a.show()
		return
	}
	a.quitting = true
	a.save()
	mygo.App.Quit()
}
func (a *application) bindKeys() {
	for _, k := range a.registered {
		mygo.GlobalShortcut.Unregister(k)
	}
	a.registered = nil
	keys := []struct {
		key string
		fn  func()
	}{{a.store.Config.ShotKey, func() { a.screenshot("region") }}, {a.store.Config.RecordKey, func() { a.record("region") }}, {a.store.Config.StopKey, a.stop}}
	for _, k := range keys {
		if e := mygo.GlobalShortcut.Register(k.key, k.fn); e != nil {
			a.status = "快捷键注册失败（可能被占用）: " + k.key
		} else {
			a.registered = append(a.registered, k.key)
		}
	}
}
func (a *application) fail(e error) { a.status = e.Error(); a.show() }
func (a *application) screenshot(mode string) {
	if a.busy || a.rec != nil {
		return
	}
	if _, e := enginePath(); e != nil {
		a.fail(e)
		return
	}
	path, e := a.store.Config.Output(".png")
	if e != nil {
		a.fail(e)
		return
	}
	a.save()
	a.busy = true
	a.win.Hide()
	go func() {
		time.Sleep(220 * time.Millisecond)
		out, e := runEngine("snip", path, mode)
		a.dispatch(func() {
			a.busy = false
			if e != nil {
				a.fail(e)
				return
			}
			if strings.HasPrefix(out, "SAVED") {
				a.store.Add(path, "截图")
				a.save()
				a.status = "截图已保存并复制到剪贴板"
				if strings.Contains(out, "clipboard_failed") {
					a.status = "截图已保存，但剪贴板被占用，复制失败"
				}
			} else {
				a.status = "已取消截图"
			}
			a.show()
		})
	}()
}
func (a *application) record(mode string) {
	if a.busy || a.rec != nil {
		return
	}
	if _, e := enginePath(); e != nil {
		a.fail(e)
		return
	}
	cfg := a.store.Config
	path, e := cfg.Output(".mp4")
	if e != nil {
		a.fail(e)
		return
	}
	a.save()
	a.busy = true
	a.win.Hide()
	go func() {
		time.Sleep(220 * time.Millisecond)
		out, e := runEngine("select", mode)
		if e != nil {
			a.dispatch(func() { a.busy = false; a.fail(e) })
			return
		}
		fields := strings.Fields(out)
		if len(fields) != 5 || fields[0] != "REGION" {
			a.dispatch(func() { a.busy = false; a.status = "已取消录制"; a.show() })
			return
		}
		n, _ := strconv.Atoi(cfg.Countdown)
		for ; n > 0; n-- {
			left := n
			a.dispatch(func() { a.status = fmt.Sprintf("%d 秒后开始录制…", left) })
			time.Sleep(time.Second)
		}
		args := append([]string{"record", path}, fields[1:]...)
		args = append(args, cfg.FPS, cfg.Bitrate(), cfg.AudioMode(), strconv.FormatBool(cfg.Cursor))
		a.dispatch(func() {
			en, e := startEngine(args, func(event string) {
				if event == "READY" {
					a.dispatch(func() {
						a.busy = false
						a.recording = true
						a.started = time.Now()
						a.status = "录制中"
						a.showControl()
					})
				}
			})
			if e != nil {
				a.busy = false
				a.fail(e)
				return
			}
			a.rec = en
			go func() {
				err := en.wait()
				a.dispatch(func() {
					a.busy = false
					a.recording = false
					a.paused = false
					a.stopping = false
					a.rec = nil
					if a.control != nil {
						a.control.Destroy()
						a.control = nil
					}
					if err != nil {
						a.fail(err)
						return
					}
					if info, e := os.Stat(path); e != nil || info.Size() < 128 {
						a.fail(fmt.Errorf("没有生成有效录像文件"))
						return
					}
					a.store.Add(path, "录屏")
					a.save()
					a.status = "录屏已保存"
					a.show()
				})
			}()
		})
	}()
}
func (a *application) stop() {
	if a.rec == nil || a.stopping {
		return
	}
	a.stopping = true
	a.status = "正在封装 MP4，请勿退出…"
	if e := a.rec.send("stop"); e != nil {
		a.status = "停止请求失败: " + e.Error()
	}
	if a.control != nil {
		a.control.Invalidate()
	}
	a.win.Invalidate()
}
func (a *application) pause() {
	if a.rec == nil || !a.recording || a.stopping {
		return
	}
	cmd := "pause"
	if a.paused {
		cmd = "resume"
	}
	if e := a.rec.send(cmd); e != nil {
		a.fail(e)
		return
	}
	a.paused = !a.paused
}
func (a *application) showControl() {
	a.control = mygo.NewWindow(mygo.WindowOptions{Title: "Sakura · 录制中", Width: 380, Height: 130, AlwaysOnTop: true, DisableResize: true, DisableMaximize: true, Content: ui.View(func(c *ui.Context) {
		ui.Column(c).Fill().Padding(14).Gap(10).Children(func() {
			label := "● 正在录制"
			if a.paused {
				label = "Ⅱ 已暂停"
			}
			if a.stopping {
				label = "正在保存…"
			}
			ui.Text(c, label).Bold()
			ui.Row(c).Gap(8).Children(func() {
				p := "暂停"
				if a.paused {
					p = "继续"
				}
				ui.Button(c, p).Disabled(a.stopping).OnClick(a.pause)
				ui.PrimaryButton(c, "停止并保存").Disabled(a.stopping).OnClick(a.stop)
				ui.Text(c, a.store.Config.StopKey).FontSize(11)
			})
		})
	})})
	a.control.OnClose(func(e *mygo.CloseEvent) { e.PreventDefault(); a.stop() })
	// The platform helper applies capture exclusion to every window of this process.
	excludeWindow(a.control.NativeHandle())
}
func section(c *ui.Context, title, detail string, body func()) {
	ui.Column(c).FillWidth().Padding(20).Gap(12).Radius(12).Background(ui.RGB(255, 255, 255)).Children(func() {
		ui.Text(c, title).FontSize(18).Bold()
		if detail != "" {
			ui.Text(c, detail).FontSize(12).TextColor(ui.RGB(105, 110, 120))
		}
		body()
	})
}
func (a *application) view(c *ui.Context) {
	ui.Column(c).Fill().Background(ui.RGB(245, 244, 240)).Children(func() {
		ui.Column(c).FillWidth().Padding(26, 28, 18, 28).Gap(6).Children(func() {
			ui.Text(c, "SAKURA / CAPTURE").FontSize(12).Bold().TextColor(ui.RGB(166, 70, 85))
			ui.Row(c).Gap(12).Children(func() {
				ui.Text(c, "把这一刻，留下来。").FontSize(28).Bold()
				ui.Text(c, version).FontSize(11).TextColor(ui.RGB(120, 120, 120))
			})
			ui.Text(c, "原生桌面 · 本地保存 · 无需登录").FontSize(12).TextColor(ui.RGB(100, 105, 115))
			ui.Segmented(c, &a.tab, "捕捉", "最近文件", "设置")
		})
		ui.Scroll(c).Grow(1).FillWidth().Children(func() {
			ui.Column(c).FillWidth().Padding(0, 28, 20, 28).Gap(14).Children(func() {
				switch a.tab {
				case 0:
					a.captureView(c)
				case 1:
					a.historyView(c)
				case 2:
					a.settingsView(c)
				}
			})
		})
		ui.Row(c).FillWidth().Padding(12, 28).Children(func() { ui.Text(c, a.status).FontSize(12).TextColor(ui.RGB(105, 95, 100)) })
	})
}
func (a *application) captureView(c *ui.Context) {
	disabled := a.busy || a.rec != nil
	section(c, "截图", "框选、标注、复制，一气呵成。", func() {
		ui.Row(c).Gap(10).Children(func() {
			ui.PrimaryButton(c, "区域截图").Disabled(disabled).OnClick(func() { a.screenshot("region") })
			ui.Button(c, "屏幕截图").Disabled(disabled).OnClick(func() { a.screenshot("screen") })
			ui.Button(c, "窗口截图").Disabled(disabled).OnClick(func() { a.screenshot("window") })
		})
		ui.Text(c, "快捷键  "+a.store.Config.ShotKey+"    /    Esc 取消").FontSize(12)
	})
	section(c, "录屏", "记录操作，也记录你的讲解。", func() {
		ui.Row(c).Gap(12).Children(func() {
			ui.Text(c, "声音")
			ui.Select(c, &a.store.Config.Audio, []string{"无声音", "系统声音", "麦克风", "系统 + 麦克风"}).Width(190)
			ui.Text(c, "帧率")
			ui.Select(c, &a.store.Config.FPS, []string{"15", "30", "60"}).Width(85)
		})
		ui.Row(c).Gap(12).Children(func() {
			ui.Text(c, "画质")
			ui.Select(c, &a.store.Config.Quality, []string{"节省空间 · 3 Mbps", "标准 · 6 Mbps", "清晰 · 12 Mbps"}).Width(190)
			ui.Checkbox(c, &a.store.Config.Cursor, "录入鼠标")
		})
		ui.Row(c).Gap(10).Children(func() {
			ui.PrimaryButton(c, "区域录屏").Disabled(disabled).OnClick(func() { a.record("region") })
			ui.Button(c, "屏幕录屏").Disabled(disabled).OnClick(func() { a.record("screen") })
			ui.Button(c, "窗口区域录屏").Disabled(disabled).OnClick(func() { a.record("window") })
		})
		ui.Text(c, "开始  "+a.store.Config.RecordKey+"    /    停止  "+a.store.Config.StopKey).FontSize(12)
	})
	section(c, "保存在你的电脑", "不会上传截图和录屏。关闭主窗口后仍可通过托盘使用。", func() {
		ui.Text(c, a.store.Config.Directory).FontSize(12)
		ui.Button(c, "打开保存文件夹").OnClick(func() {
			if e := os.MkdirAll(a.store.Config.Directory, 0700); e != nil {
				a.fail(e)
				return
			}
			mygo.Shell.OpenPath(a.store.Config.Directory)
		})
	})
}
func (a *application) historyView(c *ui.Context) {
	if len(a.store.History) == 0 {
		section(c, "还没有捕捉记录", "第一张截图、第一段录屏，会出现在这里。", func() {})
		return
	}
	ui.Row(c).Gap(10).Children(func() {
		ui.Text(c, "最近 80 条本地记录").Grow(1)
		ui.Button(c, "清空记录（不删除文件）").OnClick(func() { a.store.History = nil; a.save() })
	})
	for i, it := range a.store.History {
		it := it
		ui.Column(c.Key(strconv.Itoa(i))).FillWidth().Padding(14).Gap(6).Radius(10).Background(ui.RGB(255, 255, 255)).Children(func() {
			ui.Text(c, filepath.Base(it.Path)).Bold()
			ui.Text(c, it.Kind+" · "+it.Created.Format("2006-01-02 15:04")).FontSize(11)
			ui.Row(c).Gap(8).Children(func() {
				ui.Button(c, "打开").OnClick(func() {
					if e := mygo.Shell.OpenPath(it.Path); e != nil {
						a.fail(e)
					}
				})
				ui.Button(c, "显示位置").OnClick(func() { mygo.Shell.ShowItemInFolder(it.Path) })
			})
		})
	}
}
func (a *application) settingsView(c *ui.Context) {
	section(c, "保存与录制", "设置只保存在当前用户的本地配置目录。", func() {
		ui.Text(c, "保存目录").FontSize(12)
		ui.TextInput(c, &a.store.Config.Directory).FillWidth()
		ui.Button(c, "选择文件夹").OnClick(func() {
			go func() {
				p, e := mygo.Dialog.Open(mygo.OpenDialogOptions{Parent: a.win, Directory: true})
				a.dispatch(func() {
					if e != nil {
						a.fail(e)
					} else if len(p) > 0 {
						a.store.Config.Directory = p[0]
						a.save()
					}
				})
			}()
		})
		ui.Row(c).Gap(10).Children(func() {
			ui.Text(c, "录制倒计时（秒）")
			ui.Select(c, &a.store.Config.Countdown, []string{"0", "3", "5"}).Width(90)
		})
	})
	section(c, "全局快捷键", "可使用 Ctrl、Alt、Shift 组合。保存时会检查是否被占用。", func() {
		ui.Text(c, "区域截图")
		ui.TextInput(c, &a.store.Config.ShotKey).FillWidth()
		ui.Text(c, "区域录屏")
		ui.TextInput(c, &a.store.Config.RecordKey).FillWidth()
		ui.Text(c, "停止录屏")
		ui.TextInput(c, &a.store.Config.StopKey).FillWidth()
		ui.PrimaryButton(c, "保存设置并应用快捷键").OnClick(func() { a.store.Normalize(); a.status = "设置已保存"; a.save(); a.bindKeys() })
	})
	section(c, "关于 Sakura Capture", "Go + MyGo 原生界面 / Windows Media Foundation + WASAPI", func() {
		ui.Text(c, "窗口录屏固定记录所选窗口的屏幕区域，不会跟随移动；\n最小化或被遮挡后的画面以屏幕实际显示为准。\nWindows N 版本需安装 Media Feature Pack。").FontSize(12)
		ui.Button(c, "项目与使用说明").OnClick(func() { mygo.Shell.OpenExternal("https://github.com/yueseqaz/sakura-capture") })
	})
}
