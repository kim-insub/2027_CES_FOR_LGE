//go:build !windows

package main

import "errors"

// pickBinFile: the Windows "Open" dialog only exists on Windows (development builds type the path).
func pickBinFile(initialDir string) (string, error) {
	return "", errors.New("파일 선택 창은 Windows 에서만 됩니다. 경로를 직접 입력하세요")
}
