package main

import (
	"bytes"
	"fmt"
	"image/png"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"sync"
	"syscall"
	"testing"
	"time"
	"unsafe"

	"github.com/egoist/mygo/ui"
	"github.com/yueseqaz/sakura-capture/internal/core"
)

// These tests operate actual HWNDs, not the headless view tester. CI runs them
// after building both executables on an interactive Windows runner desktop.
type interactionLog struct {
	sync.Mutex
	buffer bytes.Buffer
}

func (b *interactionLog) Write(p []byte) (int, error) {
	b.Lock()
	defer b.Unlock()
	return b.buffer.Write(p)
}
func (b *interactionLog) text() string {
	b.Lock()
	defer b.Unlock()
	return b.buffer.String()
}

type interactionProcess struct {
	cmd  *exec.Cmd
	log  interactionLog
	done chan struct{}
	err  error
}

func launchInteraction(t *testing.T, cmd *exec.Cmd) *interactionProcess {
	t.Helper()
	p := &interactionProcess{cmd: cmd, done: make(chan struct{})}
	cmd.Stdout, cmd.Stderr = &p.log, &p.log
	if err := cmd.Start(); err != nil {
		t.Fatal(err)
	}
	go func() { p.err = cmd.Wait(); close(p.done) }()
	t.Cleanup(func() { _ = cmd.Process.Kill(); <-p.done })
	return p
}

type interactionSearch struct {
	pid          uint32
	class, title string
	hwnd         uintptr
}

var interactionEnumMutex sync.Mutex
var interactionCurrentSearch *interactionSearch

var interactionEnum = syscall.NewCallback(func(w, _ uintptr) uintptr {
	s := interactionCurrentSearch
	var pid uint32
	user32.NewProc("GetWindowThreadProcessId").Call(w, uintptr(unsafe.Pointer(&pid)))
	visible, _, _ := user32.NewProc("IsWindowVisible").Call(w)
	if pid != s.pid || visible == 0 {
		return 1
	}
	var class, title [256]uint16
	user32.NewProc("GetClassNameW").Call(w, uintptr(unsafe.Pointer(&class[0])), 256)
	user32.NewProc("GetWindowTextW").Call(w, uintptr(unsafe.Pointer(&title[0])), 256)
	if (s.class == "" || syscall.UTF16ToString(class[:]) == s.class) &&
		(s.title == "" || syscall.UTF16ToString(title[:]) == s.title) {
		s.hwnd = w
		return 0
	}
	return 1
})

func interactionWindow(pid int, class, title string) uintptr {
	interactionEnumMutex.Lock()
	defer interactionEnumMutex.Unlock()
	s := interactionSearch{pid: uint32(pid), class: class, title: title}
	interactionCurrentSearch = &s
	defer func() { interactionCurrentSearch = nil }()
	user32.NewProc("EnumWindows").Call(interactionEnum, 0)
	return s.hwnd
}

func eventuallyInteraction(t *testing.T, description string, p *interactionProcess, f func() bool) {
	t.Helper()
	until := time.Now().Add(20 * time.Second)
	for time.Now().Before(until) {
		if f() {
			return
		}
		select {
		case <-p.done:
			t.Fatalf("%s: process exited: %v\n%s", description, p.err, p.log.text())
		default:
		}
		time.Sleep(80 * time.Millisecond)
	}
	t.Fatalf("timeout: %s\n%s", description, p.log.text())
}

func interactionMessage(w uintptr, msg uint32, wp, lp uintptr) {
	user32.NewProc("SendMessageW").Call(w, uintptr(msg), wp, lp)
}
func interactionPoint(x, y int) uintptr { return uintptr(uint32(uint16(x)) | uint32(uint16(y))<<16) }
func interactionClick(w uintptr, x, y int) {
	lp := interactionPoint(x, y)
	interactionMessage(w, 0x200, 0, lp)
	interactionMessage(w, 0x201, 1, lp)
	interactionMessage(w, 0x202, 0, lp)
	time.Sleep(120 * time.Millisecond)
}
func interactionKey(w uintptr, key uintptr) {
	interactionMessage(w, 0x100, key, 0)
	interactionMessage(w, 0x101, key, 0)
}
func interactionDrag(w uintptr, x, y, xx, yy int) {
	interactionMessage(w, 0x201, 1, interactionPoint(x, y))
	interactionMessage(w, 0x200, 1, interactionPoint(xx, yy))
	interactionMessage(w, 0x202, 0, interactionPoint(xx, yy))
	time.Sleep(180 * time.Millisecond)
}

func interactionDist(t *testing.T) string {
	t.Helper()
	d := os.Getenv("SAKURA_WINDOWS_E2E")
	if d == "" {
		t.Skip("set SAKURA_WINDOWS_E2E to built Windows dist directory")
	}
	return d
}

func TestWindowsEngineLaunchFlags(t *testing.T) {
	cmd := exec.Command("sakura-engine.exe")
	prepareCommand(cmd)
	if cmd.SysProcAttr == nil || cmd.SysProcAttr.HideWindow || cmd.SysProcAttr.CreationFlags&0x08000000 == 0 {
		t.Fatal("hide only the console; STARTF_USESHOWWINDOW can suppress the selection overlay")
	}
}

func TestWindowsScreenshotInteraction(t *testing.T) {
	dist := interactionDist(t)
	for _, legacy := range []bool{false, true} {
		t.Run(fmt.Sprintf("screen-legacy-hide-%v", legacy), func(t *testing.T) {
			out := filepath.Join(t.TempDir(), "screen.png")
			cmd := exec.Command(filepath.Join(dist, "sakura-engine.exe"), "snip", out, "screen")
			prepareCommand(cmd)
			if legacy {
				cmd.SysProcAttr = &syscall.SysProcAttr{HideWindow: true}
			}
			p := launchInteraction(t, cmd)
			var w uintptr
			eventuallyInteraction(t, "visible screen selection", p, func() bool {
				w = interactionWindow(cmd.Process.Pid, "SakuraCaptureOverlay", "")
				return w != 0
			})
			interactionClick(w, 150, 150)
			interactionKey(w, 13)
			select {
			case <-p.done:
				if p.err != nil || !strings.Contains(p.log.text(), "SAVED") {
					t.Fatalf("save: %v %s", p.err, p.log.text())
				}
			case <-time.After(10 * time.Second):
				t.Fatal("screen selection stuck after click and Enter")
			}
			f, err := os.Open(out)
			if err != nil {
				t.Fatal(err)
			}
			defer f.Close()
			im, err := png.DecodeConfig(f)
			if err != nil || im.Width < 640 || im.Height < 480 {
				t.Fatalf("invalid screen PNG: %+v %v", im, err)
			}
		})
	}
	for _, width := range []int{3, 9} {
		t.Run(fmt.Sprintf("annotation-%dpx", width), func(t *testing.T) {
			out := filepath.Join(t.TempDir(), "annotation.png")
			cmd := exec.Command(filepath.Join(dist, "sakura-engine.exe"), "snip", out, "region")
			prepareCommand(cmd)
			p := launchInteraction(t, cmd)
			var w uintptr
			eventuallyInteraction(t, "visible region selection", p, func() bool { w = interactionWindow(cmd.Process.Pid, "SakuraCaptureOverlay", ""); return w != 0 })
			interactionDrag(w, 60, 80, 560, 380)
			interactionDrag(w, 140, 140, 400, 260) // default rectangle, initially 3px
			if width == 9 {
				interactionKey(w, 'V')
				interactionClick(w, 240, 190)
				// Region toolbar starts at (60,390); restyle the existing mark.
				interactionClick(w, 60+242+8+17, 390+65)
			}
			interactionKey(w, 13)
			select {
			case <-p.done:
				if p.err != nil {
					t.Fatal(p.err, p.log.text())
				}
			case <-time.After(10 * time.Second):
				t.Fatal("annotation save timed out")
			}
			f, err := os.Open(out)
			if err != nil {
				t.Fatal(err)
			}
			defer f.Close()
			im, err := png.Decode(f)
			if err != nil {
				t.Fatal(err)
			}
			// Count the exported rectangle edge, away from its corners.
			red := 0
			for y := 52; y < 70; y++ {
				r, g, b, _ := im.At(180, y).RGBA()
				if r > 45000 && g < 40000 && b < 45000 {
					red++
				}
			}
			if red < width-1 || red > width+1 {
				t.Fatalf("existing mark width: requested %d, exported red pixels=%d", width, red)
			}
		})
	}

	t.Run("cancel-and-reselect", func(t *testing.T) {
		cmd := exec.Command(filepath.Join(dist, "sakura-engine.exe"), "snip", filepath.Join(t.TempDir(), "unused.png"), "region")
		prepareCommand(cmd)
		p := launchInteraction(t, cmd)
		var w uintptr
		eventuallyInteraction(t, "cancellable selection", p, func() bool { w = interactionWindow(cmd.Process.Pid, "SakuraCaptureOverlay", ""); return w != 0 })
		interactionDrag(w, 60, 80, 560, 380)
		interactionDrag(w, 140, 140, 400, 260)
		interactionMessage(w, 0x204, 0, interactionPoint(200, 200)) // right-click reselect
		interactionKey(w, 27)
		select {
		case <-p.done:
			if p.err != nil || !strings.Contains(p.log.text(), "CANCELLED") {
				t.Fatal("selection did not cancel cleanly", p.err, p.log.text())
			}
		case <-time.After(10 * time.Second):
			t.Fatal("Esc left selection running")
		}
	})
	t.Run("move-resize-color", func(t *testing.T) {
		out := filepath.Join(t.TempDir(), "edited.png")
		cmd := exec.Command(filepath.Join(dist, "sakura-engine.exe"), "snip", out, "region")
		prepareCommand(cmd)
		p := launchInteraction(t, cmd)
		var w uintptr
		eventuallyInteraction(t, "editable screenshot", p, func() bool { w = interactionWindow(cmd.Process.Pid, "SakuraCaptureOverlay", ""); return w != 0 })
		interactionDrag(w, 60, 80, 560, 380)
		interactionDrag(w, 140, 140, 400, 260)
		interactionKey(w, 'V')
		interactionDrag(w, 240, 190, 260, 210)
		interactionDrag(w, 420, 280, 440, 300)
		interactionClick(w, 146, 455) // current color opens a popover
		interactionClick(w, 153, 184) // blue preset in the popover
		interactionKey(w, 13)
		select {
		case <-p.done:
			if p.err != nil {
				t.Fatal(p.err, p.log.text())
			}
		case <-time.After(10 * time.Second):
			t.Fatal("edited screenshot did not save")
		}
		f, err := os.Open(out)
		if err != nil {
			t.Fatal(err)
		}
		defer f.Close()
		im, err := png.Decode(f)
		if err != nil {
			t.Fatal(err)
		}
		for _, pt := range [][2]int{{200, 80}, {380, 150}, {200, 220}} {
			r, g, b, _ := im.At(pt[0], pt[1]).RGBA()
			if r != 36*257 || g != 124*257 || b != 232*257 {
				t.Fatalf("edited blue edge missing at %v: %d,%d,%d", pt, r/257, g/257, b/257)
			}
		}
	})
	t.Run("copy-without-file", func(t *testing.T) {
		out := filepath.Join(t.TempDir(), "must-not-exist.png")
		cmd := exec.Command(filepath.Join(dist, "sakura-engine.exe"), "snip", out, "region")
		prepareCommand(cmd)
		p := launchInteraction(t, cmd)
		var w uintptr
		eventuallyInteraction(t, "copy-only screenshot", p, func() bool { w = interactionWindow(cmd.Process.Pid, "SakuraCaptureOverlay", ""); return w != 0 })
		interactionDrag(w, 60, 80, 560, 380)
		interactionClick(w, 60+8+13*53+25, 390+26)
		select {
		case <-p.done:
			if p.err != nil || !strings.Contains(p.log.text(), "COPIED") {
				t.Fatal(p.err, p.log.text())
			}
		case <-time.After(10 * time.Second):
			t.Fatal("copy-only did not finish")
		}
		if _, err := os.Stat(out); !os.IsNotExist(err) {
			t.Fatalf("copy-only created a file: %v", err)
		}
	})
}

func interactionClickLabel(t *testing.T, w uintptr, view func(*ui.Context), text string) {
	t.Helper()
	class, _ := syscall.UTF16PtrFromString("MyGoSurface")
	surface, _, _ := user32.NewProc("FindWindowExW").Call(w, 0, uintptr(unsafe.Pointer(class)), 0)
	if surface == 0 {
		t.Fatal("native MyGo content surface missing")
	}
	w = surface
	var r winRect
	user32.NewProc("GetClientRect").Call(w, uintptr(unsafe.Pointer(&r)))
	dpi, _, _ := user32.NewProc("GetDpiForWindow").Call(w)
	scale := float32(dpi) / 96
	tt := ui.NewTester(view, int(float32(r.Right)/scale), int(float32(r.Bottom)/scale))
	b, ok := tt.Find(text)
	if !ok {
		t.Fatalf("missing %q in %v", text, tt.Texts())
	}
	interactionClick(w, int((b.X+b.W/2)*scale), int((b.Y+b.H/2)*scale))
}

// A plain white native window is a deterministic background for the recorder's
// exact BitBlt capture path. Its message pump lives on its owning OS thread.
func interactionWhiteWindow(t *testing.T, r winRect) uintptr {
	t.Helper()
	ready := make(chan uintptr, 1)
	done := make(chan struct{})
	go func() {
		runtime.LockOSThread()
		defer runtime.UnlockOSThread()
		defer close(done)
		class, _ := syscall.UTF16PtrFromString("STATIC")
		w, _, _ := user32.NewProc("CreateWindowExW").Call(0, uintptr(unsafe.Pointer(class)), 0, 0x90000006, uintptr(r.Left), uintptr(r.Top), uintptr(r.Right-r.Left), uintptr(r.Bottom-r.Top), 0, 0, 0, 0)
		if w != 0 {
			user32.NewProc("UpdateWindow").Call(w)
		}
		ready <- w
		if w == 0 {
			return
		}
		// WM_CLOSE destroys STATIC but does not post WM_QUIT. Use a timer to
		// keep the pump waking until IsWindow reports destruction.
		user32.NewProc("SetTimer").Call(w, 1, 50, 0)
		var msg struct {
			Hwnd           uintptr
			Message        uint32
			WParam, LParam uintptr
			Time           uint32
			X, Y           int32
			Private        uint32
		}
		for {
			alive, _, _ := user32.NewProc("IsWindow").Call(w)
			if alive == 0 {
				break
			}
			n, _, _ := user32.NewProc("GetMessageW").Call(uintptr(unsafe.Pointer(&msg)), 0, 0, 0)
			if n == 0 || int32(n) == -1 {
				break
			}
			user32.NewProc("TranslateMessage").Call(uintptr(unsafe.Pointer(&msg)))
			user32.NewProc("DispatchMessageW").Call(uintptr(unsafe.Pointer(&msg)))
		}
	}()
	w := <-ready
	if w == 0 {
		t.Fatal("white background window creation failed")
	}
	t.Cleanup(func() {
		user32.NewProc("PostMessageW").Call(w, 0x10, 0, 0)
		select {
		case <-done:
		case <-time.After(time.Second):
		}
	})
	return w
}

func interactionCapturePixel(t *testing.T, x, y int) uint32 {
	t.Helper()
	gdi := syscall.NewLazyDLL("gdi32.dll")
	dc, _, _ := user32.NewProc("GetDC").Call(0)
	defer user32.NewProc("ReleaseDC").Call(0, dc)
	mem, _, _ := gdi.NewProc("CreateCompatibleDC").Call(dc)
	defer gdi.NewProc("DeleteDC").Call(mem)
	bmp, _, _ := gdi.NewProc("CreateCompatibleBitmap").Call(dc, 1, 1)
	defer gdi.NewProc("DeleteObject").Call(bmp)
	old, _, _ := gdi.NewProc("SelectObject").Call(mem, bmp)
	defer gdi.NewProc("SelectObject").Call(mem, old)
	ok, _, _ := gdi.NewProc("BitBlt").Call(mem, 0, 0, 1, 1, dc, uintptr(x), uintptr(y), 0x40cc0020)
	if ok == 0 {
		t.Fatal("BitBlt failed")
	}
	gdi.NewProc("GdiFlush").Call()
	pixel, _, _ := gdi.NewProc("GetPixel").Call(mem, 0, 0)
	return uint32(pixel)
}

func TestWindowsRecordingDockInteraction(t *testing.T) {
	dist := interactionDist(t)
	root := t.TempDir()
	s := core.Defaults()
	s.Config.Directory = root
	s.Config.Countdown = "0"
	s.Config.Audio = "无声音"
	s.Config.FPS = "15"
	if err := s.Save(filepath.Join(root, "SakuraCapture", "settings.json")); err != nil {
		t.Fatal(err)
	}
	cmd := exec.Command(filepath.Join(dist, "SakuraCapture.exe"))
	prepareCommand(cmd)
	cmd.Env = append(os.Environ(), "APPDATA="+root)
	p := launchInteraction(t, cmd)
	a := &application{store: s, recording: true}
	var main, overlay, dock uintptr
	eventuallyInteraction(t, "persistent startup dock", p, func() bool { dock = interactionWindow(cmd.Process.Pid, "", dockTitle); return dock != 0 })
	originalDock := dock
	// A second instance opens the secondary main window; startup itself stays compact.
	second := exec.Command(filepath.Join(dist, "SakuraCapture.exe"))
	prepareCommand(second)
	second.Env = cmd.Env
	if output, err := second.CombinedOutput(); err != nil {
		t.Fatalf("second instance: %v %s", err, output)
	}
	eventuallyInteraction(t, "main window", p, func() bool { main = interactionWindow(cmd.Process.Pid, "", "Sakura Capture"); return main != 0 })
	interactionClickLabel(t, main, a.view, "屏幕录屏")
	eventuallyInteraction(t, "recording selection", p, func() bool {
		// The selector is a child process: enumerate by class independently.
		return findInteractionOverlay(&overlay)
	})
	interactionClick(overlay, 150, 150)
	eventuallyInteraction(t, "visible recording dock", p, func() bool {
		dock = interactionWindow(cmd.Process.Pid, "", dockTitle)
		return dock != 0
	})
	eventuallyInteraction(t, "encoder READY", p, func() bool { return strings.Contains(p.log.text(), "capture engine: READY") })
	var affinity uint32
	ok, _, _ := user32.NewProc("GetWindowDisplayAffinity").Call(dock, uintptr(unsafe.Pointer(&affinity)))
	if ok == 0 || affinity != 0x11 {
		t.Fatalf("dock capture exclusion not confirmed: %x", affinity)
	}
	style, _, _ := user32.NewProc("GetWindowLongW").Call(dock, ^uintptr(19)) // GWL_EXSTYLE = -20
	if style&8 == 0 {
		t.Fatal("dock is not always-on-top")
	}
	var bounds winRect
	user32.NewProc("GetWindowRect").Call(dock, uintptr(unsafe.Pointer(&bounds)))
	interactionWhiteWindow(t, bounds)
	time.Sleep(500 * time.Millisecond)
	if pixel := interactionCapturePixel(t, int(bounds.Left)+25, int(bounds.Top)+25); pixel != 0xffffff {
		t.Fatalf("dock appears in recorder capture, pixel=%06x (expected white background)", pixel)
	}
	interactionClickLabel(t, dock, a.controlView, "暂停")
	eventuallyInteraction(t, "actual pause acknowledgement", p, func() bool { return strings.Contains(p.log.text(), "capture engine: PAUSED") })
	a.paused = true
	interactionClickLabel(t, dock, a.controlView, "继续")
	eventuallyInteraction(t, "actual resume acknowledgement", p, func() bool { return strings.Contains(p.log.text(), "capture engine: RESUMED") })
	a.paused = false
	time.Sleep(500 * time.Millisecond)
	interactionClickLabel(t, dock, a.controlView, "停止并保存")
	eventuallyInteraction(t, "recording saved and persistent dock restored", p, func() bool {
		return strings.Contains(p.log.text(), "capture engine: SAVED") && interactionWindow(cmd.Process.Pid, "", dockTitle) == originalDock
	})
	files, _ := filepath.Glob(filepath.Join(root, "*.mp4"))
	if len(files) != 1 {
		t.Fatalf("expected one recording: %v\n%s", files, p.log.text())
	}
	inspect := exec.Command(filepath.Join(dist, "sakura-engine.exe"), "inspect", files[0])
	prepareCommand(inspect)
	if output, err := inspect.CombinedOutput(); err != nil {
		t.Fatalf("recording decode validation: %v %s", err, output)
	}
	a.recording = false
	time.Sleep(500 * time.Millisecond)
	interactionClickLabel(t, dock, a.dockView, "截图")
	eventuallyInteraction(t, "screenshot directly from restored dock", p, func() bool { return findInteractionOverlay(&overlay) })
	interactionKey(overlay, 27)
	eventuallyInteraction(t, "dock returns after screenshot cancellation", p, func() bool { return interactionWindow(cmd.Process.Pid, "", dockTitle) == originalDock })
	// Exercise real native resizing, not only a headless collapsed view.
	interactionClickLabel(t, dock, a.dockView, "收纳")
	eventuallyInteraction(t, "collapsed side tab", p, func() bool {
		var r winRect
		user32.NewProc("GetWindowRect").Call(dock, uintptr(unsafe.Pointer(&r)))
		return r.Right-r.Left < 100
	})
	interactionClickLabel(t, dock, (&application{dockCollapsed: true}).dockView, "捕捉")
	eventuallyInteraction(t, "side tab expands", p, func() bool {
		var r winRect
		user32.NewProc("GetWindowRect").Call(dock, uintptr(unsafe.Pointer(&r)))
		return r.Right-r.Left > 400
	})
	t.Log("persistent startup/restore, screenshot cancellation, edge tab, visible topmost dock, pixel exclusion, pause/resume/stop and MP4 decoding verified")
}

var interactionCurrentOverlay uintptr

var interactionOverlayEnum = syscall.NewCallback(func(w, _ uintptr) uintptr {
	var class [256]uint16
	user32.NewProc("GetClassNameW").Call(w, uintptr(unsafe.Pointer(&class[0])), 256)
	visible, _, _ := user32.NewProc("IsWindowVisible").Call(w)
	if visible != 0 && syscall.UTF16ToString(class[:]) == "SakuraCaptureOverlay" {
		interactionCurrentOverlay = w
		return 0
	}
	return 1
})

func findInteractionOverlay(w *uintptr) bool {
	interactionEnumMutex.Lock()
	defer interactionEnumMutex.Unlock()
	interactionCurrentOverlay = 0
	user32.NewProc("EnumWindows").Call(interactionOverlayEnum, 0)
	*w = interactionCurrentOverlay
	return *w != 0
}
