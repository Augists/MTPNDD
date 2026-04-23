//go:build pprof

package main

import (
	"net/http"
	_ "net/http/pprof"
	"os"
)

// Build with `-tags pprof` to include this file. When PPROF_PORT is set
// the library opens a localhost HTTP server on that port that serves
// runtime/pprof endpoints (/debug/pprof/profile, /goroutine, /heap, ...).
// Off by default — net/http adds several MB to the .so.
func init() {
	if port := os.Getenv("PPROF_PORT"); port != "" {
		go func() {
			_ = http.ListenAndServe("127.0.0.1:"+port, nil)
		}()
	}
}
