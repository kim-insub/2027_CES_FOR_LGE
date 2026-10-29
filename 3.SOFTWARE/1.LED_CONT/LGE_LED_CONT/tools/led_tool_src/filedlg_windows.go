//go:build windows

package main

import (
	"errors"
	"runtime"
	"syscall"
	"unsafe"

	"golang.org/x/sys/windows"
)

var (
	comdlg32             = windows.NewLazySystemDLL("comdlg32.dll")
	procGetOpenFileNameW = comdlg32.NewProc("GetOpenFileNameW")
	procCommDlgExtError  = comdlg32.NewProc("CommDlgExtendedError")
	user32               = windows.NewLazySystemDLL("user32.dll")
	procGetForegroundWnd = user32.NewProc("GetForegroundWindow")
)

// OPENFILENAMEW (commdlg.h)
type openFileName struct {
	lStructSize       uint32
	hwndOwner         uintptr
	hInstance         uintptr
	lpstrFilter       *uint16
	lpstrCustomFilter *uint16
	nMaxCustFilter    uint32
	nFilterIndex      uint32
	lpstrFile         *uint16
	nMaxFile          uint32
	lpstrFileTitle    *uint16
	nMaxFileTitle     uint32
	lpstrInitialDir   *uint16
	lpstrTitle        *uint16
	Flags             uint32
	nFileOffset       uint16
	nFileExtension    uint16
	lpstrDefExt       *uint16
	lCustData         uintptr
	lpfnHook          uintptr
	lpTemplateName    *uint16
	pvReserved        uintptr
	dwReserved        uint32
	FlagsEx           uint32
}

const (
	ofnPathMustExist = 0x00000800
	ofnFileMustExist = 0x00001000
	ofnNoChangeDir   = 0x00000008
	ofnExplorer      = 0x00080000
	ofnHideReadOnly  = 0x00000004
)

// pickBinFile shows the standard Windows "Open" dialog (*.bin) and returns the chosen path,
// or "" when the user cancels.
func pickBinFile(initialDir string) (string, error) {
	type result struct {
		path string
		err  error
	}
	ch := make(chan result, 1)
	go func() {
		// The dialog needs its own STA thread.
		runtime.LockOSThread()
		defer runtime.UnlockOSThread()
		if err := windows.CoInitializeEx(0, windows.COINIT_APARTMENTTHREADED); err == nil {
			defer windows.CoUninitialize()
		}

		filter := utf16z("펌웨어 파일 (*.bin)\x00*.bin\x00모든 파일 (*.*)\x00*.*\x00\x00")
		title, _ := syscall.UTF16PtrFromString("펌웨어 bin 파일 선택")
		buf := make([]uint16, 1024)
		owner, _, _ := procGetForegroundWnd.Call() // our app window: the dialog opens on top of it
		ofn := openFileName{
			hwndOwner:   owner,
			lpstrFilter: &filter[0],
			lpstrFile:   &buf[0],
			nMaxFile:    uint32(len(buf)),
			lpstrTitle:  title,
			Flags:       ofnPathMustExist | ofnFileMustExist | ofnNoChangeDir | ofnExplorer | ofnHideReadOnly,
		}
		ofn.lStructSize = uint32(unsafe.Sizeof(ofn))
		if initialDir != "" {
			if p, err := syscall.UTF16PtrFromString(initialDir); err == nil {
				ofn.lpstrInitialDir = p
			}
		}
		r, _, _ := procGetOpenFileNameW.Call(uintptr(unsafe.Pointer(&ofn)))
		if r == 0 {
			if code, _, _ := procCommDlgExtError.Call(); code != 0 {
				ch <- result{"", errors.New("파일 선택 창 오류")}
				return
			}
			ch <- result{"", nil} // cancelled
			return
		}
		ch <- result{windows.UTF16ToString(buf), nil}
	}()
	r := <-ch
	return r.path, r.err
}

// utf16z converts a string that already contains its \x00 separators (no extra terminator added).
func utf16z(s string) []uint16 {
	out := make([]uint16, 0, len(s)+1)
	for _, r := range s {
		if r < 0x10000 {
			out = append(out, uint16(r))
		} else {
			r -= 0x10000
			out = append(out, uint16(0xD800+(r>>10)), uint16(0xDC00+(r&0x3FF)))
		}
	}
	return out
}
