// Package benchstats counts the frames a view builds and prints the rate
// each second when MYGO_BENCH_STATS is set, as MYZIG_STATS does for MyZig.
package benchstats

import (
	"fmt"
	"os"
	"time"
)

var (
	on     = os.Getenv("MYGO_BENCH_STATS") != ""
	frames int
	since  time.Time
)

// Frame counts a frame.
func Frame() {
	if !on {
		return
	}
	now := time.Now()
	if since.IsZero() {
		since = now
	}
	frames++
	if d := now.Sub(since); d >= time.Second {
		fmt.Fprintf(os.Stderr, "mygo 1s: fps=%.1f\n", float64(frames)/d.Seconds())
		frames, since = 0, now
	}
}
