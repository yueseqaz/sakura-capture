package main

import (
	"fmt"
	"image"
	"os/exec"
	"syscall"
	"unsafe"
)

var user32 = syscall.NewLazyDLL("user32.dll")

// STARTF_USESHOWWINDOW/SW_HIDE suppresses the engine's first overlay ShowWindow.
// CREATE_NO_WINDOW hides only the console and leaves normal GUI windows usable.
func prepareCommand(c *exec.Cmd) {
	c.SysProcAttr = &syscall.SysProcAttr{CreationFlags: 0x08000000}
}

func excludeWindow(w uintptr) error {
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
