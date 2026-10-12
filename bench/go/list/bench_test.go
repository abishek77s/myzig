package main

import (
	"testing"

	"github.com/egoist/mygo/ui"
)

// BenchmarkScrolling scrolls the list by 4 DIPs a frame, at 2× density.
func BenchmarkScrolling(b *testing.B) {
	s := &items{}
	tt := ui.NewTester(s.view, 640, 720)
	tt.SetScale(2)
	b.ReportAllocs()
	dy := float32(4)
	n := 0
	start := s.frames
	for b.Loop() {
		n++
		if n%2000 == 0 {
			dy = -dy
		}
		tt.Scroll(320, 400, 0, dy)
	}
	b.ReportMetric(float64(b.Elapsed().Nanoseconds())/float64(s.frames-start), "ns/frame")
}
