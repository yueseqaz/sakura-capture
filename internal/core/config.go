package core

import (
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"time"
)

type Config struct {
	Directory string `json:"directory"`
	FPS       string `json:"fps"`
	Audio     string `json:"audio"`
	Quality   string `json:"quality"`
	Countdown string `json:"countdown"`
	Cursor    bool   `json:"cursor"`
	ShotKey   string `json:"screenshot_key"`
	RecordKey string `json:"record_key"`
	StopKey   string `json:"stop_key"`
	DockX     int    `json:"dock_x"`
	DockY     int    `json:"dock_y"`
	DockSaved bool   `json:"dock_saved"`
	DockEdge  string `json:"dock_edge,omitempty"`
}
type Item struct {
	Path    string    `json:"path"`
	Kind    string    `json:"kind"`
	Created time.Time `json:"created"`
}
type Store struct {
	Config  Config `json:"config"`
	History []Item `json:"history"`
}

func Defaults() Store {
	home, _ := os.UserHomeDir()
	return Store{Config: Config{Directory: filepath.Join(home, "Pictures", "Sakura Capture"), FPS: "30", Audio: "系统声音", Quality: "标准 · 6 Mbps", Countdown: "3", Cursor: true, ShotKey: "Ctrl+Shift+2", RecordKey: "Ctrl+Shift+3", StopKey: "Ctrl+Shift+4"}}
}
func Load(path string) (Store, error) {
	s := Defaults()
	b, e := os.ReadFile(path)
	if errors.Is(e, os.ErrNotExist) {
		return s, nil
	}
	if e != nil {
		return s, e
	}
	if e = json.Unmarshal(b, &s); e != nil {
		return Defaults(), fmt.Errorf("读取设置失败: %w", e)
	}
	s.Normalize()
	return s, nil
}
func (s *Store) Normalize() {
	d := Defaults().Config
	if s.Config.Directory == "" {
		s.Config.Directory = d.Directory
	}
	if !oneOf(s.Config.FPS, "15", "30", "60") {
		s.Config.FPS = d.FPS
	}
	if !oneOf(s.Config.Audio, "无声音", "系统声音", "麦克风", "系统 + 麦克风") {
		s.Config.Audio = d.Audio
	}
	if !oneOf(s.Config.Quality, "节省空间 · 3 Mbps", "标准 · 6 Mbps", "清晰 · 12 Mbps") {
		s.Config.Quality = d.Quality
	}
	if !oneOf(s.Config.Countdown, "0", "3", "5") {
		s.Config.Countdown = d.Countdown
	}
	if s.Config.ShotKey == "" {
		s.Config.ShotKey = d.ShotKey
	}
	if s.Config.RecordKey == "" {
		s.Config.RecordKey = d.RecordKey
	}
	if s.Config.StopKey == "" {
		s.Config.StopKey = d.StopKey
	}
	if len(s.History) > 80 {
		s.History = s.History[:80]
	}
}
func oneOf(v string, values ...string) bool {
	for _, s := range values {
		if v == s {
			return true
		}
	}
	return false
}
func (s *Store) Save(path string) error {
	if e := os.MkdirAll(filepath.Dir(path), 0700); e != nil {
		return e
	}
	b, e := json.MarshalIndent(s, "", "  ")
	if e != nil {
		return e
	}
	tmp := path + ".tmp"
	if e = os.WriteFile(tmp, b, 0600); e != nil {
		return e
	}
	return os.Rename(tmp, path)
}
func (s *Store) Add(path, kind string) {
	s.History = append([]Item{{Path: path, Kind: kind, Created: time.Now()}}, s.History...)
	if len(s.History) > 80 {
		s.History = s.History[:80]
	}
}
func (c Config) Output(ext string) (string, error) {
	if e := os.MkdirAll(c.Directory, 0700); e != nil {
		return "", e
	}
	return filepath.Join(c.Directory, "Sakura-"+time.Now().Format("20060102-150405.000000000")+ext), nil
}
func (c Config) AudioMode() string {
	switch c.Audio {
	case "系统声音":
		return "system"
	case "麦克风":
		return "mic"
	case "系统 + 麦克风":
		return "both"
	}
	return "none"
}
func (c Config) Bitrate() string {
	switch c.Quality {
	case "节省空间 · 3 Mbps":
		return "3000000"
	case "清晰 · 12 Mbps":
		return "12000000"
	}
	return "6000000"
}
