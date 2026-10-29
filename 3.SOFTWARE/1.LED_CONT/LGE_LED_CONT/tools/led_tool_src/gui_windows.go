//go:build windows

package main

import (
	"os"
	"os/exec"
	"path/filepath"
	"syscall"

	"golang.org/x/sys/windows"
)

func fatalBox(msg string) {
	t, _ := windows.UTF16PtrFromString("LED 검사·업데이트")
	m, _ := windows.UTF16PtrFromString(msg)
	_, _ = windows.MessageBox(0, m, t, windows.MB_OK|windows.MB_ICONERROR)
}

// launchWindow opens the UI as a stand-alone app window (Edge / Chrome --app), falling back to the default browser.
func launchWindow(url string) bool {
	pf := os.Getenv("ProgramFiles")
	pf86 := os.Getenv("ProgramFiles(x86)")
	local := os.Getenv("LocalAppData")
	cands := []string{
		filepath.Join(pf86, `Microsoft\Edge\Application\msedge.exe`),
		filepath.Join(pf, `Microsoft\Edge\Application\msedge.exe`),
		filepath.Join(pf, `Google\Chrome\Application\chrome.exe`),
		filepath.Join(pf86, `Google\Chrome\Application\chrome.exe`),
		filepath.Join(local, `Google\Chrome\Application\chrome.exe`),
	}
	profile := filepath.Join(os.TempDir(), "led_tool_profile")
	for _, c := range cands {
		if _, err := os.Stat(c); err != nil {
			continue
		}
		cmd := exec.Command(c, "--app="+url, "--window-size=1180,960", "--user-data-dir="+profile,
			"--no-first-run", "--no-default-browser-check", "--disable-extensions")
		cmd.SysProcAttr = &syscall.SysProcAttr{HideWindow: false}
		if cmd.Start() == nil {
			return true
		}
	}
	return exec.Command("rundll32", "url.dll,FileProtocolHandler", url).Start() == nil
}
