// Quotes is MyZig's examples/quotes.zig in MyGo: a table of 40 prices, a
// quarter of which move every frame.
package main

import (
	"log"
	"math/rand/v2"

	"github.com/egoist/mygo"
	"github.com/egoist/mygo/ui"
	"myzig.bench/mygo/benchstats"
)

const rows = 40

type quotes struct {
	symbols     [rows]string
	price, open [rows]float64
	volume      [rows]uint64
	rng         *rand.Rand
	ticks       uint64
}

func newQuotes() *quotes {
	q := &quotes{rng: rand.New(rand.NewPCG(42, 42))}
	for i := range rows {
		b := make([]byte, 4)
		for j := range b {
			b[j] = 'A' + byte(q.rng.IntN(26))
		}
		q.symbols[i] = string(b)
		q.price[i] = 20 + q.rng.Float64()*480
		q.open[i] = q.price[i]
		q.volume[i] = uint64(q.rng.IntN(1_000_000))
	}
	return q
}

func (q *quotes) tick() {
	q.ticks++
	for range rows / 4 {
		i := q.rng.IntN(rows)
		q.price[i] *= 1 + (q.rng.Float64()-0.5)*0.004
		q.volume[i] += uint64(q.rng.IntN(500))
	}
}

func (q *quotes) view(c *ui.Context) {
	benchstats.Frame()
	q.tick()
	c.AnimationFrame()
	t := c.Theme()
	ui.Column(c).Fill().Padding(16).Gap(2).Children(func() {
		ui.Textf(c, "Streaming quotes · tick %d", q.ticks).FontSize(18).Bold()
		ui.Row(c).Gap(12).Padding(0, 8).Height(24).AlignItems(ui.Center).Children(func() {
			for _, h := range []struct {
				s string
				w float32
				a ui.Align
			}{{"Symbol", 80, ui.Start}, {"Price", 90, ui.End}, {"Change", 80, ui.End}, {"%", 80, ui.End}, {"Volume", 100, ui.End}} {
				ui.Text(c, h.s).Width(h.w).TextAlign(h.a).FontSize(12).TextColor(t.TextMuted)
			}
		})
		for i := range rows {
			bg := t.Surface
			if i%2 == 1 {
				bg = ui.Color{}
			}
			ui.Row(c).Gap(12).Padding(0, 8).Height(22).AlignItems(ui.Center).Background(bg).Radius(4).Children(func() {
				ch := q.price[i] - q.open[i]
				col := t.Success
				if ch < 0 {
					col = t.Danger
				}
				ui.Text(c, q.symbols[i]).Width(80).Bold()
				ui.Textf(c, "%.2f", q.price[i]).Width(90).TextAlign(ui.End)
				ui.Textf(c, "%.2f", ch).Width(80).TextAlign(ui.End).TextColor(col)
				ui.Textf(c, "%.2f%%", ch/q.open[i]*100).Width(80).TextAlign(ui.End).TextColor(col)
				ui.Textf(c, "%d", q.volume[i]).Width(100).TextAlign(ui.End).TextColor(t.TextMuted)
			})
		}
	})
}

func main() {
	q := newQuotes()
	mygo.App.WhenReady(func() {
		mygo.NewWindow(mygo.WindowOptions{Title: "Quotes", Width: 560, Height: 1040, Content: ui.View(q.view)})
	})
	if err := mygo.App.Run(); err != nil {
		log.Fatal(err)
	}
}
