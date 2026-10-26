//go:build !windows

package main

import "os/exec"

func prepareCommand(c *exec.Cmd) {}
func excludeWindow(w uintptr)    {}
