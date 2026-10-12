// List is MyZig's examples/list.zig in MyGo: a virtualized list of 10,000
// rows under a header.
package main

import (
	"log"

	"github.com/egoist/mygo"
	"github.com/egoist/mygo/ui"
	"myzig.bench/mygo/benchstats"
)

const count = 10_000

type items struct {
	list   ui.ListState
	frames int
}

func (s *items) view(c *ui.Context) {
	benchstats.Frame()
	s.frames++
	t := c.Theme()
	ui.Column(c).Fill().Children(func() {
		ui.Row(c).Padding(12).Gap(8).AlignItems(ui.Center).Background(t.Surface).Children(func() {
			ui.Text(c, "Inbox").FontSize(16).Bold()
			ui.Textf(c, "%d messages", count).FontSize(12).TextColor(t.TextMuted)
		})
		ui.List(c, &s.list, count, func(i int) {
			ui.Row(c).Height(36).Padding(0, 12).Gap(12).AlignItems(ui.Center).Children(func() {
				dot, col := "○", t.Border
				if i%3 == 0 {
					dot, col = "●", t.Accent
				}
				ui.Text(c, dot).FontSize(10).TextColor(col)
				name := ui.Textf(c, "Message %d", i).Width(140)
				if i%7 == 0 {
					name.Bold()
				}
				ui.Textf(c, "Subject line for item %d, with a preview", i*7919%10007).TextColor(t.TextMuted).Grow(1)
				ui.Textf(c, "%02d:%02d", (i/60)%24, i%60).FontSize(12).TextColor(t.TextMuted)
			})
		}).Grow(1)
	})
}

func main() {
	s := &items{}
	mygo.App.WhenReady(func() {
		mygo.NewWindow(mygo.WindowOptions{Title: "List", Width: 640, Height: 720, Content: ui.View(s.view)})
	})
	if err := mygo.App.Run(); err != nil {
		log.Fatal(err)
	}
}
