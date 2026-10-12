package main

import (
	"testing"

	"github.com/egoist/mygo/ui"
)

// BenchmarkStreaming builds frames of the quotes, a quarter of whose
// prices move each frame, at 2× density.
func BenchmarkStreaming(b *testing.B) {
	q := newQuotes()
	tt := ui.NewTester(q.view, 560, 1040)
	tt.SetScale(2)
	b.ReportAllocs()
	start := q.ticks
	for b.Loop() {
		tt.Frame()
	}
	// The Tester runs an animated view up to 20 frames a call.
	b.ReportMetric(float64(b.Elapsed().Nanoseconds())/float64(q.ticks-start), "ns/frame")
}
