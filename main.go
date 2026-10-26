package main

import (
	"bytes"
	"context"
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
	systemAudio, micAudio                       bool
	systemPending, micPending                   bool
	audioNotice                                 string
	recordArea                                  image.Rectangle
	registered                                  []string
	cancelPreparation                           context.CancelFunc
	dockCollapsed, dockPinned, dockAdjusting    bool
	dockEdge                                    string
	dockOutside                                 time.Time
	dockLast                                    image.Rectangle
}

func main() {
	mygo.App.SetName("Sakura Capture")
	if !mygo.App.RequestSingleInstanceLock() {
		return
	}
	mygo.Theme.SetSource(mygo.ThemeLight)
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
	mygo.App.OnSecondInstance(func([]string, string) { a.dispatch(a.show) })
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
	a.win = mygo.NewWindow(mygo.WindowOptions{Title: "Sakura Capture", Width: 760, Height: 800, MinWidth: 640, MinHeight: 620, Hidden: true, Content: ui.View(a.view)})
	a.win.OnClose(func(e *mygo.CloseEvent) {
		if !a.quitting && (a.busy || a.rec != nil) && a.tray == nil {
			e.PreventDefault()
			a.status = "系统托盘不可用，请先结束截图或录制再关闭。"
			a.win.Invalidate()
			return
		}
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
	a.ensureDock()
	a.restoreDock()
	if a.tray == nil {
		a.show()
	}
	go func() {
		ticker := time.NewTicker(250 * time.Millisecond)
		defer ticker.Stop()
		for range ticker.C {
			mygo.App.WhenReady(func() {
				if !a.quitting {
					a.dockTick()
				}
			})
		}
	}()
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
	if a.quitting || a.busy || a.rec != nil {
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
func (a *application) fail(e error) { a.status = e.Error(); a.restoreDock(); a.show() }
func (a *application) screenshot(mode string) {
	if a.quitting || a.busy || a.rec != nil {
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
	a.hideDock()
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
			} else if strings.HasPrefix(out, "COPIED") {
				a.status = "截图已复制到剪贴板（未保存文件）"
			} else {
				a.status = "已取消截图"
			}
			a.restoreDock()
		})
	}()
}
func (a *application) record(mode string) {
	if a.quitting || a.busy || a.rec != nil {
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
	ctx, cancel := context.WithCancel(context.Background())
	a.save()
	a.busy = true
	a.cancelPreparation = cancel
	a.win.Hide()
	a.hideDock()
	go func() {
		time.Sleep(220 * time.Millisecond)
		out, e := runEngineContext(ctx, "select", mode)
		if e != nil {
			a.dispatch(func() {
				wasCanceled := ctx.Err() != nil
				a.endPreparation()
				if !wasCanceled {
					a.fail(e)
				}
			})
			return
		}
		fields := strings.Fields(out)
		if out == "CANCELLED" {
			a.dispatch(a.endPreparation)
			return
		}
		area, err := parseRegion(out)
		if err != nil {
			a.dispatch(func() { a.endPreparation(); a.fail(err) })
			return
		}
		n, _ := strconv.Atoi(cfg.Countdown)
		for ; n > 0; n-- {
			left := n
			a.dispatch(func() {
				if ctx.Err() != nil {
					return
				}
				a.status = fmt.Sprintf("%d 秒后开始录制…", left)
				a.showCountdown()
			})
			select {
			case <-ctx.Done():
				a.dispatch(a.endPreparation)
				return
			case <-time.After(time.Second):
			}
		}
		args := append([]string{"record", path}, fields[1:]...)
		args = append(args, cfg.FPS, cfg.Bitrate(), cfg.AudioMode(), strconv.FormatBool(cfg.Cursor))
		a.dispatch(func() {
			if ctx.Err() != nil {
				a.endPreparation()
				return
			}
			a.cancelPreparation = nil
			cancel()
			a.systemAudio, a.micAudio = false, false
			a.systemPending, a.micPending = true, true
			a.audioNotice = ""
			a.recordArea = area
			a.elapsed = 0
			// Show a protected dock before encoder/audio initialization, not only
			// after READY; otherwise a slow or failed startup looks like a hang.
			if e := a.showControl(); e != nil {
				a.endPreparation()
				a.fail(e)
				return
			}
			en, e := startEngine(args, func(event string) {
				log.Printf("capture engine: %s", event)
				if strings.HasPrefix(event, "AUDIO") {
					a.dispatch(func() { a.audioEvent(event) })
					return
				}
				if event == "READY" {
					a.dispatch(func() {
						if a.rec == nil || a.stopping {
							return
						}
						a.busy = false
						a.recording = true
						a.started = time.Now()
						a.elapsed = 0
						a.status = "录制中"
					})
				}
			})
			if e != nil {
				a.endPreparation()
				a.fail(e)
				return
			}
			a.rec = en
			done := make(chan struct{})
			go func() {
				ticker := time.NewTicker(time.Second)
				defer ticker.Stop()
				for {
					select {
					case <-done:
						return
					case <-ticker.C:
						a.dispatch(func() {
							if a.rec == en && a.recording && !a.paused && !a.stopping {
								a.elapsed++
							}
						})
					}
				}
			}()
			go func() {
				err := en.wait()
				close(done)
				a.dispatch(func() {
					a.busy = false
					a.recording = false
					a.paused = false
					a.stopping = false
					a.rec = nil
					a.restoreDock()
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
					a.restoreDock()
				})
			}()
		})
	}()
}
func (a *application) stop() {
	if a.cancelPreparation != nil {
		a.cancelPreparation()
		return
	}
	if a.rec == nil || a.stopping {
		return
	}
	a.stopping = true
	a.status = "正在封装 MP4，请勿退出…"
	if e := a.rec.send("stop"); e != nil {
		a.stopping = false
		a.status = "停止请求失败: " + e.Error()
		a.audioNotice = "停止请求发送失败，请重试。"
	}
	if a.control != nil {
		a.control.Invalidate()
	}
	if a.win != nil {
		a.win.Invalidate()
	}
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
func (a *application) showControl() error {
	a.ensureDock()
	// Apply exclusion before showing the control, avoiding a captured first frame.
	if err := excludeWindow(a.control.NativeHandle()); err != nil {
		return err
	}
	a.dockCollapsed = false
	a.dockPinned = true
	a.sizeDock(520, 200)
	a.control.ShowInactive()
	a.control.Invalidate()
	return nil
}

func parseRegion(out string) (image.Rectangle, error) {
	f := strings.Fields(out)
	if len(f) == 5 && f[0] == "REGION" {
		var n [4]int
		valid := true
		for i := range n {
			v, err := strconv.Atoi(f[i+1])
			n[i] = v
			valid = valid && err == nil && v >= -100000 && v <= 100000
		}
		if valid && n[2] >= 4 && n[3] >= 4 && n[2] <= 7680 && n[3] <= 4320 {
			return image.Rect(n[0], n[1], n[0]+n[2], n[1]+n[3]), nil
		}
	}
	return image.Rectangle{}, fmt.Errorf("采集引擎未返回有效选区，请完整解压新版程序后重试。返回内容: %.200s", out)
}
func (a *application) endPreparation() {
	if a.cancelPreparation != nil {
		a.cancelPreparation()
		a.cancelPreparation = nil
	}
	a.busy = false
	a.status = "已取消录制"
	a.restoreDock()
}
func (a *application) showCountdown() {
	a.ensureDock()
	a.dockCollapsed = false
	a.sizeDock(520, 156)
	a.control.ShowInactive()
	a.control.Invalidate()
}
func section(c *ui.Context, title, detail string, body func()) {
	ui.Column(c).FillWidth().Padding(22).Gap(12).Radius(16).Border(1, line).Background(ui.Hex("#fffdfb")).Children(func() {
		ui.Text(c, title).FontSize(18).Bold()
		if detail != "" {
			ui.Text(c, detail).FontSize(12).TextColor(muted)
		}
		body()
	})
}
func (a *application) view(c *ui.Context) {
	c.SetTheme(sakuraTheme(false))
	ui.Column(c).Fill().Background(ui.Hex("#f6f3ef")).Children(func() {
		ui.Column(c).FillWidth().Padding(24, 28, 18, 28).Gap(14).Children(func() {
			ui.Row(c).FillWidth().AlignItems(ui.Center).Gap(10).Children(func() {
				ui.Column(c).Size(36, 36).Radius(11).Background(rose).AlignItems(ui.Center).Justify(ui.Center).Children(func() {
					ui.Icon(c, shotIcon).Size(22, 22).TextColor(ui.Hex("#ffffff"))
				})
				ui.Text(c, "SAKURA").Font("Bahnschrift").FontSize(20).Bold()
				ui.Text(c, "DESKTOP COMPANION").Grow(1).FontSize(10).TextColor(muted)
				ui.Text(c, version).FontSize(11).TextColor(muted)
			})
			ui.Segmented(c, &a.tab, "捕捉", "最近文件", "设置").FillWidth()
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
		ui.Row(c).FillWidth().Padding(12, 28).BorderWidth(1, 0, 0, 0).BorderColor(line).Gap(8).Children(func() {
			ui.Text(c, "●").FontSize(10).TextColor(rose)
			ui.Text(c, a.status).Grow(1).MinWidth(0).FontSize(12).TextColor(muted)
		})
	})
}
func (a *application) captureView(c *ui.Context) {
	disabled := a.busy || a.rec != nil
	ui.Row(c).FillWidth().Padding(20, 22).Radius(16).Gradient(ui.Hex("#efe1dd"), ui.Hex("#f9eee5"), 20).Gap(16).AlignItems(ui.Center).Children(func() {
		ui.Column(c).Grow(1).Gap(6).Children(func() {
			ui.Text(c, "随时在旁，随手捕捉。").FontSize(26).Bold()
			ui.Text(c, "原生桌面 · 本地保存 · 无需登录").FontSize(12).TextColor(ui.Hex("#896b6b"))
		})
		ui.Column(c).Gap(4).AlignItems(ui.End).Children(func() {
			ui.Text(c, "悬浮常驻 · 贴边收纳").FontSize(12).Bold().TextColor(rose)
			ui.Text(c, "PNG / MP4").Font("Consolas").FontSize(11).TextColor(ui.Hex("#896b6b"))
		})
	})
	section(c, "01  /  截图", "可编辑标注 · 连续粗细 · 自定义颜色与拾色 · 复制或保存。", func() {
		ui.Row(c).Gap(10).Wrap().Children(func() {
			actionButton(c, "区域截图", shotIcon, true).Disabled(disabled).OnClick(func() { a.screenshot("region") })
			ui.Button(c, "屏幕截图").Height(38).Disabled(disabled).OnClick(func() { a.screenshot("screen") })
			ui.Button(c, "窗口截图").Height(38).Disabled(disabled).OnClick(func() { a.screenshot("window") })
		})
		ui.Text(c, "选择移动 / 箭头 / 高亮 / 文字 / 马赛克   ·   "+a.store.Config.ShotKey).FontSize(11).TextColor(muted)
	})
	section(c, "02  /  录屏", "记录操作，也记录讲解。悬浮控制台随时暂停和切换声音。", func() {
		ui.Row(c).Gap(12).Wrap().Children(func() {
			ui.Text(c, "声音").TextColor(muted)
			ui.Select(c, &a.store.Config.Audio, []string{"无声音", "系统声音", "麦克风", "系统 + 麦克风"}).Width(186).Disabled(disabled)
			ui.Text(c, "帧率").TextColor(muted)
			ui.Select(c, &a.store.Config.FPS, []string{"15", "30", "60"}).Width(80).Disabled(disabled)
		})
		ui.Row(c).Gap(12).Wrap().Children(func() {
			ui.Text(c, "画质").TextColor(muted)
			ui.Select(c, &a.store.Config.Quality, []string{"节省空间 · 3 Mbps", "标准 · 6 Mbps", "清晰 · 12 Mbps"}).Width(186).Disabled(disabled)
			ui.Checkbox(c, &a.store.Config.Cursor, "录入鼠标").Disabled(disabled)
		})
		ui.Row(c).Gap(10).Wrap().Children(func() {
			actionButton(c, "区域录屏", videoIcon, true).Disabled(disabled).OnClick(func() { a.record("region") })
			ui.Button(c, "屏幕录屏").Height(38).Disabled(disabled).OnClick(func() { a.record("screen") })
			ui.Button(c, "窗口区域录屏").Height(38).Disabled(disabled).OnClick(func() { a.record("window") })
		})
		ui.Text(c, "开始  "+a.store.Config.RecordKey+"    /    停止  "+a.store.Config.StopKey).FontSize(11).TextColor(muted)
		if a.recording {
			ui.Button(c, "显示录制控制台").OnClick(func() {
				if a.control != nil {
					a.control.Show()
					a.control.Focus()
				}
			})
		}
	})
	ui.Row(c).FillWidth().Padding(15, 18).Gap(12).AlignItems(ui.Center).Radius(12).Border(1, line).Children(func() {
		ui.Icon(c, folderIcon).Size(22, 22).TextColor(rose)
		ui.Column(c).Grow(1).MinWidth(0).Gap(4).Children(func() {
			ui.Text(c, "只在你的电脑里").FontSize(12).Bold()
			ui.Text(c, a.store.Config.Directory).FillWidth().FontSize(11).TextColor(muted)
		})
		ui.Button(c, "打开文件夹").OnClick(func() {
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
		ui.Column(c.Key(strconv.Itoa(i))).FillWidth().Padding(14).Gap(6).Radius(10).Background(ui.Hex("#fffdfb")).Children(func() {
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
	section(c, "桌面悬浮工具栏", "启动即常驻；拖动至左右边缘，移开鼠标后自动收纳。", func() {
		ui.Row(c).Gap(10).Children(func() {
			ui.Button(c, "展开并固定工具栏").OnClick(func() {
				a.expandDock(true)
				if a.control != nil {
					a.control.ShowInactive()
				}
			})
			ui.Button(c, "贴边收纳").OnClick(a.collapseDock)
		})
		ui.Text(c, "截图时自动隐藏，结束后恢复。录制时切换为控制台；收纳后悬停可展开。完整退出请使用托盘菜单。 ").FillWidth().FontSize(12)
	})
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
