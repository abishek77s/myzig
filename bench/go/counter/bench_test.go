package main

import (
	"testing"

	"github.com/egoist/mygo/ui"
)

// BenchmarkUnchanged builds frames of the counter that show what the last
// one showed, at 2× density.
func BenchmarkUnchanged(b *testing.B) {
	s := &counter{}
	tt := ui.NewTester(s.view, 360, 260)
	tt.SetScale(2)
	b.ReportAllocs()
	for b.Loop() {
		tt.Frame()
	}
}
