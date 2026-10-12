// Counter is MyGo's examples/counter-native, unchanged but for counting
// frames.
package main

import (
	"log"

	"github.com/egoist/mygo"
	"github.com/egoist/mygo/ui"
	"myzig.bench/mygo/benchstats"
)

type counter struct{ n int }

func (s *counter) view(c *ui.Context) {
	benchstats.Frame()
	if c.Shortcut(0, ui.KeyUp) {
		s.n++
	}
	if c.Shortcut(0, ui.KeyDown) {
		s.n--
	}
	t := c.Theme()
	ui.Column(c).Fill().Center().Gap(16).Children(func() {
		ui.Textf(c, "%d", s.n).FontSize(56).Bold()
		ui.Row(c).Gap(8).Children(func() {
			if ui.Button(c.Key("decrement"), "−").Label("Decrement").Width(44).Clicked() {
				s.n--
			}
			if ui.Button(c.Key("reset"), "Reset").Disabled(s.n == 0).Clicked() {
				s.n = 0
			}
			if ui.PrimaryButton(c.Key("increment"), "+").Label("Increment").Width(44).Clicked() {
				s.n++
			}
		})
		ui.Text(c, "↑ and ↓ count too").FontSize(12).TextColor(t.TextMuted)
	})
}

func main() {
	s := &counter{}
	mygo.App.WhenReady(func() {
		mygo.NewWindow(mygo.WindowOptions{Title: "Counter", Width: 360, Height: 260, Content: ui.View(s.view)})
	})
	if err := mygo.App.Run(); err != nil {
		log.Fatal(err)
	}
}
