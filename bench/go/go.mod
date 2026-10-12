module myzig.bench/mygo

go 1.27.1

require github.com/egoist/mygo v0.0.0

require (
	github.com/ebitengine/purego v0.11.1 // indirect
	github.com/go-text/typesetting v0.3.5 // indirect
	golang.org/x/image v0.46.0 // indirect
)

// bench/run.sh clones MyGo at the commit it pins into .cache/mygo.
replace github.com/egoist/mygo => ../.cache/mygo
