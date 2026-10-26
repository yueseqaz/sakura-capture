package main

import (
	"bytes"
	"errors"
	"strings"
	"testing"

	"github.com/egoist/mygo/ui"
	"github.com/yueseqaz/sakura-capture/internal/core"
)

type controlInput struct {
	bytes.Buffer
	err error
}

func (w *controlInput) Close() error                      { return nil }
func (w *controlInput) WriteString(s string) (int, error) { return w.Write([]byte(s)) }
func (w *controlInput) Write(p []byte) (int, error) {
	if w.err != nil {
		return 0, w.err
	}
	return w.Buffer.Write(p)
}

func TestLiveAudioControls(t *testing.T) {
	w := &controlInput{}
	a := &application{store: core.Defaults(), rec: &engine{input: w}, recording: true}
	a.toggleAudio("mic")
	if w.String() != "audio mic on\n" || !a.micPending || a.micAudio {
		t.Fatal("microphone must wait for actual engine acknowledgement")
	}
	a.toggleAudio("mic")
	if strings.Count(w.String(), "audio") != 1 {
		t.Fatal("duplicate request while pending")
	}
	a.audioEvent("AUDIO mic on")
	if !a.micAudio || a.micPending {
		t.Fatal("acknowledgement not applied")
	}
	a.paused = true
	a.toggleAudio("mic")
	if !strings.HasSuffix(w.String(), "audio mic off\n") {
		t.Fatal("muting must work while paused")
	}
	a.audioEvent("AUDIO mic off")
	a.toggleAudio("system")
	a.audioEvent("AUDIO_ERROR system No default speaker device")
	if a.systemAudio || a.systemPending || a.audioNotice == "" || !a.recording {
		t.Fatal("endpoint failure must not claim enabled or stop the video")
	}
	if a.store.Config.Audio != "系统声音" {
		t.Fatal("live controls must not modify next-session defaults")
	}
	a.stopping = true
	w.Reset()
	a.toggleAudio("mic")
	if w.Len() != 0 {
		t.Fatal("audio must not change during finalization")
	}
}

func TestAudioCommandFailure(t *testing.T) {
	w := &controlInput{err: errors.New("closed pipe")}
	a := &application{rec: &engine{input: w}, recording: true}
	a.toggleAudio("mic")
	if a.micPending || a.micAudio || a.audioNotice == "" {
		t.Fatal("failed request must not leave pending or enabled state")
	}
	for _, event := range []string{"AUDIO", "AUDIO unknown on", "AUDIO_ERROR", "junk mic on"} {
		a.audioEvent(event)
	}
}

func TestControlView(t *testing.T) {
	w := &controlInput{}
	a := &application{store: core.Defaults(), rec: &engine{input: w}, recording: true, elapsed: 3661}
	tt := ui.NewTester(a.controlView, 520, 200)
	for _, text := range []string{"01:01:01", "暂停", "停止并保存", "电脑声音", "麦克风"} {
		if !tt.HasText(text) {
			t.Errorf("missing control %q", text)
		}
	}
	if err := tt.Click("麦克风"); err != nil {
		t.Fatal(err)
	}
	if w.String() != "audio mic on\n" {
		t.Fatal("microphone button is not wired to engine")
	}
	if err := tt.Click("暂停"); err != nil {
		t.Fatal(err)
	}
	if !a.paused || !tt.HasText("继续") {
		t.Fatal("pause control did not update")
	}
	if err := tt.Click("继续"); err != nil {
		t.Fatal(err)
	}
	if a.paused || !strings.HasSuffix(w.String(), "resume\n") {
		t.Fatal("resume is not wired to engine")
	}
}
