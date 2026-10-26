//go:build !windows

package main

import (
	"image"
	"os/exec"
)

func prepareCommand(c *exec.Cmd)                         {}
func excludeWindow(w uintptr) error                      { return nil }
func placeControl(w uintptr, area image.Rectangle) error { return nil }
