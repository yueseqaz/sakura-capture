package main

import (
	"bufio"
	"bytes"
	"context"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"sync"
)

type engine struct {
	cmd        *exec.Cmd
	input      io.WriteCloser
	mu         sync.Mutex
	log        bytes.Buffer
	eventsDone chan struct{}
}

func enginePath() (string, error) {
	if runtime.GOOS != "windows" {
		return "", fmt.Errorf("采集引擎只支持 Windows。请下载 GitHub 构建的 Windows 便携版")
	}
	exe, e := os.Executable()
	if e != nil {
		return "", e
	}
	p := filepath.Join(filepath.Dir(exe), "sakura-engine.exe")
	if _, e = os.Stat(p); e != nil {
		return "", fmt.Errorf("缺少 sakura-engine.exe，请完整解压下载包: %w", e)
	}
	return p, nil
}
func runEngine(args ...string) (string, error) {
	return runEngineContext(context.Background(), args...)
}
func runEngineContext(ctx context.Context, args ...string) (string, error) {
	p, e := enginePath()
	if e != nil {
		return "", e
	}
	c := exec.CommandContext(ctx, p, args...)
	prepareCommand(c)
	var errout bytes.Buffer
	c.Stderr = &errout
	b, e := c.Output()
	if e != nil {
		return "", fmt.Errorf("%v\n%s", e, strings.TrimSpace(errout.String()))
	}
	return strings.TrimSpace(string(b)), nil
}
func startEngine(args []string, event func(string)) (*engine, error) {
	p, e := enginePath()
	if e != nil {
		return nil, e
	}
	c := exec.Command(p, args...)
	prepareCommand(c)
	en := &engine{cmd: c, eventsDone: make(chan struct{})}
	en.input, e = c.StdinPipe()
	if e != nil {
		return nil, e
	}
	out, e := c.StdoutPipe()
	if e != nil {
		return nil, e
	}
	c.Stderr = &en.log
	if e = c.Start(); e != nil {
		return nil, e
	}
	go func() {
		defer close(en.eventsDone)
		s := bufio.NewScanner(out)
		for s.Scan() {
			event(s.Text())
		}
	}()
	return en, nil
}
func (e *engine) send(s string) error {
	e.mu.Lock()
	defer e.mu.Unlock()
	_, err := io.WriteString(e.input, s+"\n")
	return err
}
func (e *engine) wait() error {
	// Drain events before Wait closes StdoutPipe, preserving final acknowledgements.
	if e.eventsDone != nil {
		<-e.eventsDone
	}
	err := e.cmd.Wait()
	if err != nil {
		return fmt.Errorf("%v\n%s", err, e.log.String())
	}
	return nil
}
