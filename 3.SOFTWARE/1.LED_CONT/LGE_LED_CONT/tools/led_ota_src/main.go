// LED BLE update (GUI) - sends a firmware .bin to a DC27 board over Bluetooth LE (no USB, no BOOT button).
//
// Same structure as the LED tester: a tiny local web server + Edge/Chrome app window. The exe talks to the
// PC's own Bluetooth adapter (Windows 10/11 WinRT, no driver or installation).
//
// Board side: src/ble_ota.h (BLE is on for 5 minutes after power-on, PIN required).
//
//	Board name : "<TYPE> 0x<ID> <DESC>"   e.g. "LED 0x10 DC27_FRT"
//	.bin tag   : "DC27FW|<TYPE>|0x<ID>|<VERSION>|"   (read from the file before sending, checked again by the board)
//	GATT       : INFO (read tag) / CTRL (BEGIN, END, notify results) / DATA (offset + payload, 4096 B blocks)
package main

import (
	"context"
	"crypto/sha256"
	_ "embed"
	"encoding/binary"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"sync"
	"time"

	"tinygo.org/x/bluetooth"
)

//go:embed ui.html
var uiHTML []byte

var (
	uuidSvc  = bluetooth.NewUUID([16]byte{0x4c, 0x47, 0x45, 0x01, 0xdc, 0x27, 0x4f, 0x54, 0xa0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01})
	uuidInfo = bluetooth.NewUUID([16]byte{0x4c, 0x47, 0x45, 0x01, 0xdc, 0x27, 0x4f, 0x54, 0xa0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02})
	uuidCtrl = bluetooth.NewUUID([16]byte{0x4c, 0x47, 0x45, 0x01, 0xdc, 0x27, 0x4f, 0x54, 0xa0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03})
	uuidData = bluetooth.NewUUID([16]byte{0x4c, 0x47, 0x45, 0x01, 0xdc, 0x27, 0x4f, 0x54, 0xa0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04})

	// "LED 0x10 DC27_FRT", "RF 0x20 ...", ... (every DC27 board uses this name scheme)
	reName = regexp.MustCompile(`^([A-Z]+) 0x([0-9A-Fa-f]{2})(?: (.*))?$`)
	reTag  = regexp.MustCompile(`DC27FW\|([A-Z]+)\|0x([0-9A-Fa-f]{2})\|([\x20-\x7b\x7d\x7e]{1,16})\|`)
)

const blockSize = 4096

var statusText = map[byte]string{
	0: "OK", 1: "PIN 이 틀립니다", 2: "PIN 3회 오류로 잠김 (보드 전원을 껐다 켜세요)", 3: "파일이 너무 큽니다",
	4: "보드가 다른 업데이트 중입니다", 5: "보드 플래시 준비 실패", 6: "잘못된 명령", 7: "데이터 순서 오류",
	8: "플래시 쓰기 실패", 9: "시작되지 않음", 10: "크기 불일치", 11: "SHA-256 불일치 (전송 오류)",
	12: "펌웨어 이미지 검사 실패 (ESP32-C6 용 bin 이 아님)", 13: "bin 안에 DC27FW 태그가 없음",
	14: "보드 종류가 다른 bin", 15: "보드 ID 가 다른 bin", 16: "부팅 파티션 설정 실패",
}

func stText(s byte) string {
	if t, ok := statusText[s]; ok {
		return t
	}
	return fmt.Sprintf("오류 %d", s)
}

type fwTag struct {
	Type string `json:"type"`
	ID   string `json:"id"` // "0x10"
	Ver  string `json:"ver"`
}

func parseTag(b []byte) (fwTag, bool) {
	m := reTag.FindSubmatch(b)
	if m == nil {
		return fwTag{}, false
	}
	return fwTag{string(m[1]), "0x" + strings.ToUpper(string(m[2])), string(m[3])}, true
}

type hub struct {
	mu   sync.Mutex
	subs map[chan string]struct{}
	zero time.Time
	ever bool
}

func (h *hub) sub() chan string {
	h.mu.Lock()
	defer h.mu.Unlock()
	ch := make(chan string, 512)
	h.subs[ch] = struct{}{}
	h.ever = true
	return ch
}

func (h *hub) unsub(ch chan string) {
	h.mu.Lock()
	defer h.mu.Unlock()
	delete(h.subs, ch)
	if len(h.subs) == 0 {
		h.zero = time.Now()
	}
}

func (h *hub) pub(s string) {
	h.mu.Lock()
	defer h.mu.Unlock()
	for ch := range h.subs {
		select {
		case ch <- s:
		default:
		}
	}
}

type app struct {
	h       *hub
	start   time.Time
	adapter *bluetooth.Adapter
	btErr   error

	mu       sync.Mutex
	busy     bool // scanning or updating (the adapter does one thing at a time)
	devs     map[string]bluetooth.Address
	bin      []byte
	binName  string
	binTag   fwTag
	binTagOK bool
	lastDir  string
}

func writeJSON(w http.ResponseWriter, code int, v any) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.WriteHeader(code)
	_ = json.NewEncoder(w).Encode(v)
}

func (a *app) emit(v map[string]any) {
	b, _ := json.Marshal(v)
	a.h.pub(string(b))
}

func (a *app) logf(format string, args ...any) {
	a.emit(map[string]any{"t": "log", "m": fmt.Sprintf(format, args...)})
}

func (a *app) setBusy(b bool) bool {
	a.mu.Lock()
	defer a.mu.Unlock()
	if b && a.busy {
		return false
	}
	a.busy = b
	return true
}

// ---------------------------------------------------------------- HTTP handlers

func (a *app) status(w http.ResponseWriter, r *http.Request) {
	msg := ""
	if a.btErr != nil {
		msg = a.btErr.Error()
	}
	writeJSON(w, 200, map[string]any{"bt": a.btErr == nil, "err": msg})
}

// browse: standard Windows "Open" dialog. Starts in the folder of the last file, else the project's
// bin\ folder next to tools\ (where the PlatformIO build puts LED_0x10_v1.0.bin), else the exe folder.
func (a *app) browse(w http.ResponseWriter, r *http.Request) {
	a.mu.Lock()
	dir := a.lastDir
	a.mu.Unlock()
	if dir == "" {
		if exe, err := os.Executable(); err == nil {
			dir = filepath.Dir(exe)
			if st, err := os.Stat(filepath.Join(dir, "..", "bin")); err == nil && st.IsDir() {
				dir = filepath.Clean(filepath.Join(dir, "..", "bin"))
			}
		}
	}
	p, err := pickBinFile(dir)
	if err != nil {
		writeJSON(w, 500, map[string]string{"error": err.Error()})
		return
	}
	writeJSON(w, 200, map[string]string{"path": p})
}

// loadpath: read the .bin at the given path and report its firmware tag.
func (a *app) loadPath(w http.ResponseWriter, r *http.Request) {
	var req struct {
		Path string `json:"path"`
	}
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil || strings.TrimSpace(req.Path) == "" {
		writeJSON(w, 400, map[string]string{"error": "파일 경로를 입력하세요"})
		return
	}
	path := strings.Trim(strings.TrimSpace(req.Path), `"`)
	st, err := os.Stat(path)
	if err != nil || st.IsDir() {
		writeJSON(w, 400, map[string]string{"error": "파일을 찾을 수 없습니다: " + path})
		return
	}
	if st.Size() == 0 || st.Size() > 4<<20 {
		writeJSON(w, 400, map[string]string{"error": "bin 파일 크기가 맞지 않습니다"})
		return
	}
	data, err := os.ReadFile(path)
	if err != nil {
		writeJSON(w, 400, map[string]string{"error": "파일을 읽을 수 없습니다: " + err.Error()})
		return
	}
	tag, ok := parseTag(data)
	a.mu.Lock()
	a.bin, a.binName, a.binTag, a.binTagOK = data, filepath.Base(path), tag, ok
	a.lastDir = filepath.Dir(path)
	a.mu.Unlock()
	sum := sha256.Sum256(data)
	writeJSON(w, 200, map[string]any{"path": path, "name": filepath.Base(path), "size": len(data), "tagOK": ok,
		"tag": tag, "esp": data[0] == 0xE9, "sha": fmt.Sprintf("%X", sum[:4])})
}

func (a *app) scan(w http.ResponseWriter, r *http.Request) {
	if a.btErr != nil {
		writeJSON(w, 409, map[string]string{"error": "Bluetooth 를 사용할 수 없습니다: " + a.btErr.Error()})
		return
	}
	if !a.setBusy(true) {
		writeJSON(w, 409, map[string]string{"error": "다른 작업 중입니다"})
		return
	}
	a.mu.Lock()
	a.devs = map[string]bluetooth.Address{}
	a.mu.Unlock()
	go func() {
		defer a.setBusy(false)
		a.logf("보드 찾는 중 (6초)...")
		seen := map[string]bool{}
		t := time.AfterFunc(6*time.Second, func() { _ = a.adapter.StopScan() })
		defer t.Stop()
		err := a.adapter.Scan(func(_ *bluetooth.Adapter, res bluetooth.ScanResult) {
			name := res.LocalName()
			m := reName.FindStringSubmatch(name)
			if m == nil {
				return
			}
			key := res.Address.String()
			a.mu.Lock()
			a.devs[key] = res.Address
			a.mu.Unlock()
			if !seen[key] {
				seen[key] = true
				a.logf("  발견: %s  (%s, RSSI %d dBm)", name, key, res.RSSI)
			}
			a.emit(map[string]any{"t": "dev", "addr": key, "name": name, "type": m[1],
				"id": "0x" + strings.ToUpper(m[2]), "rssi": res.RSSI})
		})
		if err != nil {
			a.logf("검색 오류: %v", err)
		}
		if len(seen) == 0 {
			a.logf("보드를 찾지 못했습니다. 보드 전원을 껐다 켠 뒤 5분 안에 다시 찾으세요.")
		}
		a.emit(map[string]any{"t": "scan_done", "n": len(seen)})
	}()
	writeJSON(w, 200, map[string]bool{"ok": true})
}

func (a *app) update(w http.ResponseWriter, r *http.Request) {
	var req struct {
		Addr  string `json:"addr"`
		Name  string `json:"name"`
		Pin   string `json:"pin"`
		Force bool   `json:"force"`
	}
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil || req.Addr == "" {
		writeJSON(w, 400, map[string]string{"error": "보드를 선택하세요"})
		return
	}
	a.mu.Lock()
	addr, okAddr := a.devs[req.Addr]
	bin, tag, tagOK := a.bin, a.binTag, a.binTagOK
	a.mu.Unlock()
	switch {
	case !okAddr:
		writeJSON(w, 400, map[string]string{"error": "보드를 다시 찾으세요"})
		return
	case bin == nil:
		writeJSON(w, 400, map[string]string{"error": "bin 파일을 먼저 선택하세요"})
		return
	case !tagOK:
		writeJSON(w, 400, map[string]string{"error": "이 bin 에는 DC27FW 태그가 없습니다 (BLE 업데이트를 지원하는 펌웨어가 아님)"})
		return
	case len(req.Pin) == 0 || len(req.Pin) > 32:
		writeJSON(w, 400, map[string]string{"error": "PIN 을 입력하세요"})
		return
	}
	if !a.setBusy(true) {
		writeJSON(w, 409, map[string]string{"error": "다른 작업 중입니다"})
		return
	}
	go func() {
		defer a.setBusy(false)
		start := time.Now()
		err := a.doUpdate(addr, req.Name, bin, tag, req.Pin, req.Force)
		if err != nil {
			a.logf("실패: %v", err)
			a.emit(map[string]any{"t": "done", "ok": false, "m": err.Error()})
			return
		}
		a.logf("완료 (%.1f 초). 보드가 새 펌웨어로 재부팅합니다.", time.Since(start).Seconds())
		a.emit(map[string]any{"t": "done", "ok": true})
	}()
	writeJSON(w, 200, map[string]bool{"ok": true})
}

// ---------------------------------------------------------------- BLE transfer

type notifyMsg struct {
	code byte
	st   byte
	off  uint32
}

func (a *app) doUpdate(addr bluetooth.Address, name string, bin []byte, tag fwTag, pin string, force bool) error {
	a.logf("연결 중: %s ...", name)
	dev, err := a.adapter.Connect(addr, bluetooth.ConnectionParams{ConnectionTimeout: bluetooth.NewDuration(10 * time.Second)})
	if err != nil {
		return fmt.Errorf("연결 실패: %w", err)
	}
	defer func() { _ = dev.Disconnect() }()

	svcs, err := dev.DiscoverServices([]bluetooth.UUID{uuidSvc})
	if err != nil || len(svcs) == 0 {
		return errors.New("업데이트 서비스가 없습니다 (BLE 업데이트를 지원하지 않는 펌웨어)")
	}
	chars, err := svcs[0].DiscoverCharacteristics([]bluetooth.UUID{uuidInfo, uuidCtrl, uuidData})
	if err != nil || len(chars) != 3 {
		return errors.New("업데이트 서비스 구성 오류")
	}
	var info, ctrl, data bluetooth.DeviceCharacteristic
	for _, c := range chars {
		switch c.UUID() {
		case uuidInfo:
			info = c
		case uuidCtrl:
			ctrl = c
		case uuidData:
			data = c
		}
	}

	// Running firmware of the board vs. the file
	buf := make([]byte, 64)
	n, err := info.Read(buf)
	if err != nil {
		return fmt.Errorf("보드 정보 읽기 실패: %w", err)
	}
	cur, ok := parseTag(buf[:n])
	if !ok {
		return fmt.Errorf("보드 정보를 알 수 없습니다: %q", string(buf[:n]))
	}
	a.logf("보드 펌웨어: %s %s v%s  ->  파일: %s %s v%s", cur.Type, cur.ID, cur.Ver, tag.Type, tag.ID, tag.Ver)
	if cur.Type != tag.Type {
		return fmt.Errorf("보드 종류(%s)와 파일 종류(%s)가 다릅니다", cur.Type, tag.Type)
	}
	if cur.ID != tag.ID && !force {
		return fmt.Errorf("보드 ID(%s)와 파일 ID(%s)가 다릅니다. ID 를 바꾸려면 'ID 변경 허용'을 체크하세요", cur.ID, tag.ID)
	}

	msgs := make(chan notifyMsg, 64)
	err = ctrl.EnableNotifications(func(b []byte) {
		if len(b) < 2 {
			return
		}
		m := notifyMsg{code: b[0], st: b[1]}
		switch {
		case b[0] == 0x82 && len(b) >= 5:
			m.st, m.off = 0, binary.LittleEndian.Uint32(b[1:5])
		case b[0] == 0x83 && len(b) >= 6:
			m.off = binary.LittleEndian.Uint32(b[2:6])
		}
		select {
		case msgs <- m:
		default:
		}
	})
	if err != nil {
		return fmt.Errorf("알림 설정 실패: %w", err)
	}
	wait := func(d time.Duration) (notifyMsg, error) {
		select {
		case m := <-msgs:
			return m, nil
		case <-time.After(d):
			return notifyMsg{}, errors.New("보드 응답 없음 (시간 초과)")
		}
	}

	// BEGIN
	sum := sha256.Sum256(bin)
	cmd := make([]byte, 0, 39+len(pin))
	cmd = append(cmd, 0x01)
	cmd = binary.LittleEndian.AppendUint32(cmd, uint32(len(bin)))
	cmd = append(cmd, sum[:]...)
	flags := byte(0)
	if force {
		flags |= 1
	}
	cmd = append(cmd, flags, byte(len(pin)))
	cmd = append(cmd, pin...)
	if _, err := ctrl.Write(cmd); err != nil {
		return fmt.Errorf("시작 명령 실패: %w", err)
	}
	m, err := wait(10 * time.Second)
	if err != nil {
		return err
	}
	if m.code != 0x81 || m.st != 0 {
		return errors.New(stText(m.st))
	}

	// payload per packet: ATT MTU - 3 (write header) - 4 (offset), at most 240
	chunk := 240
	if mtu, err := data.GetMTU(); err == nil && int(mtu)-7 < chunk {
		chunk = int(mtu) - 7
	}
	if chunk < 16 {
		chunk = 16
	}
	a.logf("전송 시작: %d 바이트, 패킷 %d 바이트", len(bin), chunk)

	pkt := make([]byte, 4+chunk)
	off := 0
	retries := 0
	lastPct := -1
	for off < len(bin) {
		end := off + blockSize
		if end > len(bin) {
			end = len(bin)
		}
		for p := off; p < end; p += chunk {
			q := p + chunk
			if q > end {
				q = end
			}
			binary.LittleEndian.PutUint32(pkt, uint32(p))
			copy(pkt[4:], bin[p:q])
			if _, err := data.WriteWithoutResponse(pkt[:4+q-p]); err != nil {
				return fmt.Errorf("전송 실패 (%d): %w", p, err)
			}
		}
		m, err := wait(8 * time.Second)
		if err != nil {
			return err
		}
		switch {
		case m.code == 0x82 && int(m.off) == end:
			off = end
			retries = 0
		case m.code == 0x83 && m.st == 7 && retries < 3: // order error: resend the block
			retries++
			a.logf("  블록 %d 재전송", m.off)
			off = int(m.off)
			time.Sleep(200 * time.Millisecond)
			for len(msgs) > 0 {
				<-msgs
			}
		case m.code == 0x83:
			return fmt.Errorf("%s (위치 %d)", stText(m.st), m.off)
		default:
			return fmt.Errorf("예상하지 못한 응답 %02X %d", m.code, m.st)
		}
		pct := off * 100 / len(bin)
		if pct != lastPct {
			lastPct = pct
			a.emit(map[string]any{"t": "progress", "pct": pct, "off": off, "size": len(bin)})
		}
	}

	// END: board checks SHA-256 + image + tag, then reboots
	a.logf("검증 중...")
	if _, err := ctrl.Write([]byte{0x02}); err != nil {
		return fmt.Errorf("종료 명령 실패: %w", err)
	}
	m, err = wait(20 * time.Second)
	if err != nil {
		return err
	}
	if m.code != 0x84 || m.st != 0 {
		return errors.New(stText(m.st))
	}
	return nil
}

// ---------------------------------------------------------------- events / main

func (a *app) events(w http.ResponseWriter, r *http.Request) {
	fl, ok := w.(http.Flusher)
	if !ok {
		http.Error(w, "stream unsupported", 500)
		return
	}
	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-cache")
	w.WriteHeader(200)
	ch := a.h.sub()
	defer a.h.unsub(ch)
	fmt.Fprint(w, ": hello\n\n")
	fl.Flush()
	tk := time.NewTicker(time.Second)
	defer tk.Stop()
	for {
		select {
		case <-r.Context().Done():
			return
		case m := <-ch:
			fmt.Fprintf(w, "data: %s\n\n", m)
			fl.Flush()
		case <-tk.C:
			fmt.Fprint(w, ": ping\n\n")
			fl.Flush()
		}
	}
}

func main() {
	noBrowser := flag.Bool("nobrowser", false, "do not open the window (development)")
	portFlag := flag.Int("http", 0, "fixed local HTTP port (default: random)")
	flag.Parse()

	ln, err := net.Listen("tcp", fmt.Sprintf("127.0.0.1:%d", *portFlag))
	if err != nil {
		fatalBox("로컬 서버를 시작할 수 없습니다: " + err.Error())
		os.Exit(1)
	}
	a := &app{h: &hub{subs: map[chan string]struct{}{}, zero: time.Now()}, start: time.Now(),
		adapter: bluetooth.DefaultAdapter, devs: map[string]bluetooth.Address{}}
	a.btErr = a.adapter.Enable()

	mux := http.NewServeMux()
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/" {
			http.NotFound(w, r)
			return
		}
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		w.Header().Set("Cache-Control", "no-store")
		_, _ = w.Write(uiHTML)
	})
	mux.HandleFunc("/api/status", a.status)
	mux.HandleFunc("/api/browse", a.browse)
	mux.HandleFunc("/api/loadpath", a.loadPath)
	mux.HandleFunc("/api/scan", a.scan)
	mux.HandleFunc("/api/update", a.update)
	mux.HandleFunc("/api/events", a.events)
	// Only same-origin requests from our own page are accepted.
	guard := http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		host := ln.Addr().String()
		if r.Host != host {
			http.Error(w, "forbidden", 403)
			return
		}
		if o := r.Header.Get("Origin"); o != "" && o != "http://"+host {
			http.Error(w, "forbidden", 403)
			return
		}
		mux.ServeHTTP(w, r)
	})
	srv := &http.Server{Handler: guard}
	go func() { _ = srv.Serve(ln) }()

	url := "http://" + ln.Addr().String() + "/"
	if *noBrowser {
		fmt.Println(url)
	} else if !launchWindow(url) {
		fatalBox("창을 열 수 없습니다. 브라우저(Edge/Chrome)가 설치되어 있는지 확인하세요.\n주소: " + url)
		os.Exit(1)
	}

	// Exit when the window is closed (no event-stream client for 4 s) and no update is running,
	// or if no window ever connected within 60 s.
	for {
		time.Sleep(500 * time.Millisecond)
		a.h.mu.Lock()
		n, ever, zero := len(a.h.subs), a.h.ever, a.h.zero
		a.h.mu.Unlock()
		a.mu.Lock()
		busy := a.busy
		a.mu.Unlock()
		if n == 0 && !busy && ((ever && time.Since(zero) > 4*time.Second) || (!ever && time.Since(a.start) > 60*time.Second)) {
			break
		}
	}
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	_ = srv.Shutdown(ctx)
}
