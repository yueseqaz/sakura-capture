package main

import "github.com/egoist/mygo/ui"

var (
	ink   = ui.Hex("#20262b")
	rose  = ui.Hex("#c5576b")
	muted = ui.Hex("#76777b")
	line  = ui.Hex("#e9e3df")
)

func sakuraTheme(dark bool) *ui.Theme {
	t := ui.LightTheme()
	t.Background, t.Text, t.TextMuted = ui.Hex("#f6f3ef"), ink, muted
	t.Surface, t.SurfaceHover, t.SurfacePressed = ui.Hex("#faf7f4"), ui.Hex("#f1e7e4"), ui.Hex("#e9d7d5")
	t.Border, t.Radius, t.FontSize = line, 9, 13
	t.Accent, t.AccentHover, t.AccentPressed = rose, ui.Hex("#ae465c"), ui.Hex("#92384a")
	t.Focus, t.Selection = ui.RGBA(197, 87, 107, .5), ui.RGBA(197, 87, 107, .2)
	if dark {
		t.Dark, t.Background, t.Text, t.TextMuted = true, ink, ui.Hex("#f9f5f1"), ui.Hex("#aab5bc")
		t.Surface, t.SurfaceHover, t.SurfacePressed = ui.Hex("#343d44"), ui.Hex("#414c54"), ui.Hex("#4b5861")
		t.Border = ui.Hex("#465159")
	}
	return t
}

func vector(path string) *ui.SVG {
	return ui.MustParseSVG([]byte(`<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round">` + path + `</svg>`))
}

var (
	shotIcon    = vector(`<path d="M8 3H3v5m13-5h5v5M3 16v5h5m8 0h5v-5"/><rect x="7" y="7" width="10" height="10" rx="2"/>`)
	videoIcon   = vector(`<rect x="3" y="5" width="13" height="14" rx="3"/><path d="m16 10 5-3v10l-5-3"/>`)
	folderIcon  = vector(`<path d="M3 7V5h7l2 3h9v12H3V7Z"/><path d="M3 10h18"/>`)
	micIcon     = vector(`<rect x="9" y="2" width="6" height="13" rx="3"/><path d="M5 10v2a7 7 0 0 0 14 0v-2m-7 9v3m-4 0h8"/>`)
	speakerIcon = vector(`<path d="m11 4-5 4H2v8h4l5 4V4Zm4 4a6 6 0 0 1 0 8m3-11a10 10 0 0 1 0 14"/>`)
	pauseIcon   = vector(`<path d="M8 5v14M16 5v14" stroke-width="3"/>`)
	playIcon    = vector(`<path d="m7 4 13 8-13 8V4Z"/>`)
	stopIcon    = vector(`<rect x="5" y="5" width="14" height="14" rx="2" fill="currentColor" stroke="none"/>`)
)

func actionButton(c *ui.Context, text string, icon *ui.SVG, primary bool) ui.Element {
	b := ui.ButtonBase(c).Padding(10, 14).Radius(9)
	if primary {
		b = b.Background(rose).TextColor(ui.Hex("#ffffff"))
	}
	return b.Children(func() {
		ui.Row(c).Gap(8).AlignItems(ui.Center).Children(func() {
			ui.Icon(c, icon).Size(17, 17)
			ui.Text(c, text).FontSize(13).Bold()
		})
	})
}
