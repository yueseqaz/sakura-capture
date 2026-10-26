package main

import (
	"os/exec"
	"syscall"
)

func prepareCommand(c *exec.Cmd) { c.SysProcAttr = &syscall.SysProcAttr{HideWindow: true} }
func excludeWindow(w uintptr) {
	syscall.NewLazyDLL("user32.dll").NewProc("SetWindowDisplayAffinity").Call(w, 0x11)
}
