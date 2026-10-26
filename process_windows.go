package main

import (
	"fmt"
	"image"
	"os/exec"
	"syscall"
	"unsafe"
)

var user32 = syscall.NewLazyDLL("user32.dll")

func prepareDockWindow(w uintptr) error {
	// A hidden caption still enforces a 136px system-menu minimum on Windows.
	// A genuine popup style allows the same HWND to shrink to the 44-DIP tab.
	style, _, err := user32.NewProc("GetWindowLongPtrW").Call(w, ^uintptr(15)) // GWL_STYLE = -16
	if style == 0 {
		return fmt.Errorf("读取悬浮窗样式失败: %v", err)
	}
	next := uint32(style)&^uint32(0x00cf0000) | 0x80000000
	old, _, err := user32.NewProc("SetWindowLongPtrW").Call(w, ^uintptr(15), uintptr(next))
	if old == 0 {
		return fmt.Errorf("初始化悬浮窗样式失败: %v", err)
	}
	user32.NewProc("SetWindowPos").Call(w, 0, 0, 0, 0, 0, 0x0001|0x0002|0x0004|0x0010|0x0020)
	corner := uint32(2)
	syscall.NewLazyDLL("dwmapi.dll").NewProc("DwmSetWindowAttribute").Call(w, 33, uintptr(unsafe.Pointer(&corner)), 4)
	return nil
}

func readDock(w uintptr) dockState {
	var r winRect
	user32.NewProc("GetWindowRect").Call(w, uintptr(unsafe.Pointer(&r)))
	monitor, _, _ := user32.NewProc("MonitorFromWindow").Call(w, 2)
	info := monitorInfo{Size: uint32(unsafe.Sizeof(monitorInfo{}))}
	user32.NewProc("GetMonitorInfoW").Call(monitor, uintptr(unsafe.Pointer(&info)))
	var p struct{ X, Y int32 }
	user32.NewProc("GetCursorPos").Call(uintptr(unsafe.Pointer(&p)))
	down, _, _ := user32.NewProc("GetAsyncKeyState").Call(1)
	return dockState{bounds: image.Rect(int(r.Left), int(r.Top), int(r.Right), int(r.Bottom)), work: image.Rect(int(info.Work.Left), int(info.Work.Top), int(info.Work.Right), int(info.Work.Bottom)), cursor: image.Pt(int(p.X), int(p.Y)), down: down&0x8000 != 0}
}

func moveDock(w uintptr, p image.Point) {
	user32.NewProc("SetWindowPos").Call(w, ^uintptr(0), uintptr(p.X), uintptr(p.Y), 0, 0, 0x0001|0x0010)
}

// STARTF_USESHOWWINDOW/SW_HIDE suppresses the engine's first overlay ShowWindow.
// CREATE_NO_WINDOW hides only the console and leaves normal GUI windows usable.
func prepareCommand(c *exec.Cmd) {
	c.SysProcAttr = &syscall.SysProcAttr{CreationFlags: 0x08000000}
}

func excludeWindow(w uintptr) error {
	// Older Windows can accept 0x11 but behave like WDA_MONITOR (a black
	// rectangle). Check the real build, not the manifest-sensitive version API.
	var version struct {
		Size, Major, Minor, Build, Platform uint32
		ServicePack                         [128]uint16
	}
	version.Size = uint32(unsafe.Sizeof(version))
	status, _, _ := syscall.NewLazyDLL("ntdll.dll").NewProc("RtlGetVersion").Call(uintptr(unsafe.Pointer(&version)))
	if status != 0 || version.Major < 10 || (version.Major == 10 && version.Build < 19041) {
		return fmt.Errorf("控制台录制排除需要 Windows 10 2004 或更新版本，当前系统不支持，已取消录制")
	}
	r, _, err := user32.NewProc("SetWindowDisplayAffinity").Call(w, 0x11)
	if r == 0 {
		return fmt.Errorf("无法将控制台排除在录屏之外（需要 Windows 10 2004 或更新版本）: %v", err)
	}
	var affinity uint32
	r, _, err = user32.NewProc("GetWindowDisplayAffinity").Call(w, uintptr(unsafe.Pointer(&affinity)))
	if r == 0 || affinity != 0x11 {
		return fmt.Errorf("Windows 未确认控制台录制排除设置，已取消录制以免控制台入镜: %v", err)
	}
	return nil
}

type winRect struct{ Left, Top, Right, Bottom int32 }
type monitorInfo struct {
	Size          uint32
	Monitor, Work winRect
	Flags         uint32
}

func placeControl(w uintptr, area image.Rectangle) error {
	center := area.Min.Add(area.Size().Div(2))
	point := uint64(uint32(center.X)) | uint64(uint32(center.Y))<<32
	monitor, _, _ := user32.NewProc("MonitorFromPoint").Call(uintptr(point), 2)
	info := monitorInfo{Size: uint32(unsafe.Sizeof(monitorInfo{}))}
	r, _, err := user32.NewProc("GetMonitorInfoW").Call(monitor, uintptr(unsafe.Pointer(&info)))
	if r == 0 {
		return fmt.Errorf("无法定位录制控制台: %v", err)
	}
	var bounds winRect
	r, _, err = user32.NewProc("GetWindowRect").Call(w, uintptr(unsafe.Pointer(&bounds)))
	if r == 0 {
		return fmt.Errorf("无法读取控制台尺寸: %v", err)
	}
	width, height := bounds.Right-bounds.Left, bounds.Bottom-bounds.Top
	x := info.Work.Left + (info.Work.Right-info.Work.Left-width)/2
	y := info.Work.Bottom - height - 24
	if x < info.Work.Left {
		x = info.Work.Left
	}
	if y < info.Work.Top {
		y = info.Work.Top
	}
	r, _, err = user32.NewProc("SetWindowPos").Call(w, ^uintptr(0), uintptr(x), uintptr(y), 0, 0, 0x0001|0x0010|0x0040)
	if r == 0 {
		return fmt.Errorf("无法显示录制控制台: %v", err)
	}
	return nil
}
