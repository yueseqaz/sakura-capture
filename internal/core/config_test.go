package core

import (
	"os"
	"path/filepath"
	"testing"
)

func TestRoundTrip(t *testing.T) {
	p := filepath.Join(t.TempDir(), "settings.json")
	s, e := Load(p)
	if e != nil {
		t.Fatal(e)
	}
	s.Config.Cursor = false
	s.Config.DockX, s.Config.DockY, s.Config.DockSaved, s.Config.DockEdge = -1920, 80, true, "left"
	s.Add("中文.png", "截图")
	if e = s.Save(p); e != nil {
		t.Fatal(e)
	}
	r, e := Load(p)
	if e != nil || r.Config.Cursor || len(r.History) != 1 {
		t.Fatalf("roundtrip: %+v %v", r, e)
	}
	if !r.Config.DockSaved || r.Config.DockX != -1920 || r.Config.DockY != 80 || r.Config.DockEdge != "left" {
		t.Fatal("floating dock placement was not persisted", r.Config)
	}
}
func TestCorruptConfig(t *testing.T) {
	p := filepath.Join(t.TempDir(), "bad")
	os.WriteFile(p, []byte("{"), 0600)
	s, e := Load(p)
	if e == nil || s.Config.FPS != "30" {
		t.Fatal("expected defaults and error")
	}
}
func TestNormalize(t *testing.T) {
	s := Store{}
	s.Normalize()
	if s.Config.FPS != "30" || s.Config.Directory == "" {
		t.Fatal(s)
	}
}
func TestHistoryBound(t *testing.T) {
	s := Defaults()
	for range 100 {
		s.Add("x", "截图")
	}
	if len(s.History) != 80 {
		t.Fatal(len(s.History))
	}
}
func TestOutput(t *testing.T) {
	s := Defaults()
	s.Config.Directory = filepath.Join(t.TempDir(), "child")
	a, e := s.Config.Output(".mp4")
	if e != nil {
		t.Fatal(e)
	}
	b, _ := s.Config.Output(".mp4")
	if a == b || filepath.Ext(a) != ".mp4" {
		t.Fatal(a, b)
	}
	seen := map[string]bool{a: true, b: true}
	for range 1000 {
		path, err := s.Config.Output(".png")
		if err != nil || seen[path] {
			t.Fatalf("rapid capture filename collision: %q %v", path, err)
		}
		seen[path] = true
	}
	files, err := os.ReadDir(s.Config.Directory)
	if err != nil || len(files) != 0 {
		t.Fatalf("output naming must not leave empty files on cancellation: %v %v", files, err)
	}
}
func TestModes(t *testing.T) {
	s := Defaults()
	s.Config.Audio = "系统 + 麦克风"
	if s.Config.AudioMode() != "both" {
		t.Fatal("audio")
	}
	s.Config.Quality = "清晰 · 12 Mbps"
	if s.Config.Bitrate() != "12000000" {
		t.Fatal("bitrate")
	}
}
