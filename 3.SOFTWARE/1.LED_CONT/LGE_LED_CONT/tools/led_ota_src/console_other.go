//go:build !windows

package main

import (
	"fmt"
	"os"
	"os/exec"
)

func fatalBox(msg string) { fmt.Fprintln(os.Stderr, msg) }

// launchWindow opens url in a browser app window (non-Windows: only used for development).
func launchWindow(url string) bool {
	return exec.Command("xdg-open", url).Start() == nil
}
