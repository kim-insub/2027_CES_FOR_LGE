// LED tester (GUI) - DC27_FRT / kiosk_neo_frt LED boards (DEVICE_ID 0x10~, last year 0x01~0x04).
//
// Same structure as the RF tester: a tiny local web server + app window. The exe opens the serial port itself
// (Windows COM port, 115200 8N1) with the same procedure as the PC lighting program and shows the test UI in an
// Edge/Chrome "app" window. No installation, no console window.
//
// Host protocol:
//
//	PC -> board : 0x00 -> board answers 1 byte DEVICE_ID
//	PC -> board : 0x01..0x28 -> LED effect command (which ones are valid depends on the DEVICE_ID)
//	PC -> board : 0xF0 0xA5 0x5A -> reboot into ROM download mode (firmware update without the BOOT button)
package main

import (
	"context"
	_ "embed"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"time"

	"go.bug.st/serial"
	"go.bug.st/serial/enumerator"
)

//go:embed ui.html
var uiHTML []byte

type hub struct {
	mu   sync.Mutex
	subs map[chan string]struct{}
	zero time.Time // when the last subscriber left
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

type conn struct {
	sp   serial.Port
	stop chan struct{}
	wmu  sync.Mutex // serialises writes (effect commands from several UI actions)
}

type app struct {
	h     *hub
	mu    sync.Mutex
	conns map[string]*conn
	start time.Time
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

func ints(b []byte) []int {
	out := make([]int, len(b))
	for i, v := range b {
		out[i] = int(v)
	}
	return out
}

// readFor emulates pyserial read(n) with a timeout: returns after n bytes or after the full timeout.
func readFor(sp serial.Port, n int, d time.Duration) []byte {
	var got []byte
	buf := make([]byte, n)
	deadline := time.Now().Add(d)
	_ = sp.SetReadTimeout(50 * time.Millisecond)
	for len(got) < n && time.Now().Before(deadline) {
		k, err := sp.Read(buf[:n-len(got)])
		if err != nil {
			break
		}
		got = append(got, buf[:k]...)
	}
	return got
}

func (a *app) closeConn(name string) {
	a.mu.Lock()
	c := a.conns[name]
	delete(a.conns, name)
	a.mu.Unlock()
	if c != nil {
		close(c.stop)
		_ = c.sp.Close()
	}
}

func (a *app) closeAll() []string {
	a.mu.Lock()
	names := make([]string, 0, len(a.conns))
	for n := range a.conns {
		names = append(names, n)
	}
	a.mu.Unlock()
	for _, n := range names {
		a.closeConn(n)
	}
	return names
}

// probe = the same open + ID request procedure as the PC programs (lge_lighting_control_for_ces / lge_rfkey_receiver_for_ces_r3):
// open 115200 8N1 (timeout 1.0 s) -> DTR False -> RTS False -> reset buffers -> wait 1.0 s ->
// read(10) pre-existing bytes -> write 0x00 -> read(4) -> first byte = DEVICE_ID.
// On success the port stays open and a reader (read 1 byte, timeout 0.5 s) is started: an LED board only ever
// answers an ID request, so any other byte is reported as stray.
func (a *app) probe(name string, keep bool) {
	a.closeConn(name)
	a.logf("Checking port %s", name)
	sp, err := serial.Open(name, &serial.Mode{BaudRate: 115200, DataBits: 8, Parity: serial.NoParity, StopBits: serial.OneStopBit})
	if err != nil {
		a.logf("  Failed to open %s: %v", name, err)
		a.emit(map[string]any{"t": "device", "port": name, "open": false, "err": err.Error(), "pre": []int{}, "resp": []int{}})
		return
	}
	_ = sp.SetDTR(false)
	_ = sp.SetRTS(false)
	_ = sp.ResetInputBuffer()
	_ = sp.ResetOutputBuffer()
	time.Sleep(1 * time.Second) // BOOT_WAIT
	pre := readFor(sp, 10, 1*time.Second)
	if len(pre) > 0 {
		a.logf("  %s: pre-existing bytes before ID request: % X", name, pre)
	}
	_, _ = sp.Write([]byte{0x00})
	resp := readFor(sp, 4, 1*time.Second)
	if len(resp) > 0 {
		a.logf("  %s: raw response bytes after 0x00: % X", name, resp)
	}
	hasID := len(resp) >= 1
	if hasID {
		a.logf("  Found device: ID=0x%02X on %s", resp[0], name)
	} else {
		a.logf("  No ID response on %s", name)
	}
	if !hasID && !keep {
		_ = sp.Close()
		a.emit(map[string]any{"t": "device", "port": name, "open": false, "pre": ints(pre), "resp": []int{}})
		return
	}
	c := &conn{sp: sp, stop: make(chan struct{})}
	a.mu.Lock()
	a.conns[name] = c
	a.mu.Unlock()
	a.emit(map[string]any{"t": "device", "port": name, "open": true, "pre": ints(pre), "resp": ints(resp)})
	go func() {
		_ = sp.SetReadTimeout(500 * time.Millisecond)
		buf := make([]byte, 1)
		for {
			select {
			case <-c.stop:
				return
			default:
			}
			n, err := sp.Read(buf)
			select {
			case <-c.stop:
				return
			default:
			}
			if err != nil {
				a.emit(map[string]any{"t": "readerr", "port": name, "err": err.Error()})
				a.closeConn(name)
				return
			}
			if n == 1 {
				a.emit(map[string]any{"t": "rx", "port": name, "b": int(buf[0])})
			}
		}
	}()
}

func listPorts() []string {
	seen := map[string]bool{}
	var out []string
	if names, err := serial.GetPortsList(); err == nil {
		for _, n := range names {
			if !seen[n] {
				seen[n] = true
				out = append(out, n)
			}
		}
	}
	sort.Strings(out)
	return out
}

func (a *app) ports(w http.ResponseWriter, r *http.Request) {
	type P struct {
		Name string `json:"name"`
		Desc string `json:"desc"`
	}
	list := []P{}
	desc := map[string]string{}
	if det, err := enumerator.GetDetailedPortsList(); err == nil {
		for _, p := range det {
			d := p.Product
			if d == "" && p.IsUSB {
				d = "USB " + p.VID + ":" + p.PID
			}
			desc[p.Name] = d
		}
	}
	for _, n := range listPorts() {
		list = append(list, P{n, desc[n]})
	}
	writeJSON(w, 200, list)
}

func (a *app) scan(w http.ResponseWriter, r *http.Request) {
	var req struct {
		Port string `json:"port"`
	}
	_ = json.NewDecoder(r.Body).Decode(&req)
	go func() {
		a.logf("Scanning serial ports...")
		if req.Port != "" {
			a.probe(req.Port, true) // explicit port: keep it listed even without an ID response
		} else {
			a.closeAll()
			for _, n := range listPorts() {
				a.probe(n, false)
			}
		}
		a.emit(map[string]any{"t": "scan_done"})
	}()
	writeJSON(w, 200, map[string]bool{"ok": true})
}

func (a *app) connect(w http.ResponseWriter, r *http.Request) {
	var req struct {
		Port string `json:"port"`
	}
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil || req.Port == "" {
		writeJSON(w, 400, map[string]string{"error": "port required"})
		return
	}
	go func() {
		a.logf("Connecting %s ...", req.Port)
		a.probe(req.Port, true)
	}()
	writeJSON(w, 200, map[string]bool{"ok": true})
}

func (a *app) disconnect(w http.ResponseWriter, r *http.Request) {
	var req struct {
		Port string `json:"port"`
	}
	_ = json.NewDecoder(r.Body).Decode(&req)
	a.closeConn(req.Port)
	a.logf("Closed port %s. You can upload firmware now.", req.Port)
	a.emit(map[string]any{"t": "closed", "port": req.Port})
	writeJSON(w, 200, map[string]bool{"ok": true})
}

// send writes 1..16 raw bytes (effect command, ID request, bootloader magic) to an already connected port.
func (a *app) send(w http.ResponseWriter, r *http.Request) {
	var req struct {
		Port  string `json:"port"`
		Bytes []int  `json:"bytes"`
	}
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil || req.Port == "" || len(req.Bytes) < 1 || len(req.Bytes) > 16 {
		writeJSON(w, 400, map[string]string{"error": "port and 1..16 bytes required"})
		return
	}
	buf := make([]byte, len(req.Bytes))
	for i, v := range req.Bytes {
		if v < 0 || v > 255 {
			writeJSON(w, 400, map[string]string{"error": "byte out of range"})
			return
		}
		buf[i] = byte(v)
	}
	a.mu.Lock()
	c := a.conns[req.Port]
	a.mu.Unlock()
	if c == nil {
		writeJSON(w, 409, map[string]string{"error": "port is not connected"})
		return
	}
	c.wmu.Lock()
	_, err := c.sp.Write(buf)
	c.wmu.Unlock()
	if err != nil {
		writeJSON(w, 500, map[string]string{"error": err.Error()})
		return
	}
	writeJSON(w, 200, map[string]bool{"ok": true})
}

// closeports = CLOSE PORTS button of the PC program: release every COM port.
func (a *app) closePorts(w http.ResponseWriter, r *http.Request) {
	names := a.closeAll()
	for _, n := range names {
		a.logf("Closed port %s.", n)
		a.emit(map[string]any{"t": "closed", "port": n})
	}
	if len(names) == 0 {
		a.logf("No open ports.")
	} else {
		a.logf("All ports closed. You can upload firmware now.")
	}
	writeJSON(w, 200, map[string]bool{"ok": true})
}

func (a *app) saveLog(w http.ResponseWriter, r *http.Request) {
	var req struct {
		Text string `json:"text"`
	}
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		writeJSON(w, 400, map[string]string{"error": "bad request"})
		return
	}
	dir := "."
	if exe, err := os.Executable(); err == nil {
		dir = filepath.Dir(exe)
	}
	p := filepath.Join(dir, "led_tester_log_"+time.Now().Format("20060102_150405")+".txt")
	if err := os.WriteFile(p, []byte(strings.ReplaceAll(req.Text, "\n", "\r\n")), 0o644); err != nil {
		p = filepath.Join(os.TempDir(), filepath.Base(p))
		if err2 := os.WriteFile(p, []byte(req.Text), 0o644); err2 != nil {
			writeJSON(w, 500, map[string]string{"error": err2.Error()})
			return
		}
	}
	writeJSON(w, 200, map[string]string{"path": p})
}

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
	a := &app{h: &hub{subs: map[chan string]struct{}{}, zero: time.Now()}, conns: map[string]*conn{}, start: time.Now()}
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
	mux.HandleFunc("/api/ports", a.ports)
	mux.HandleFunc("/api/connect", a.connect)
	mux.HandleFunc("/api/disconnect", a.disconnect)
	mux.HandleFunc("/api/send", a.send)
	mux.HandleFunc("/api/closeports", a.closePorts)
	mux.HandleFunc("/api/scan", a.scan)
	mux.HandleFunc("/api/savelog", a.saveLog)
	mux.HandleFunc("/api/events", a.events)
	// Only same-origin requests from our own page are accepted (blocks other web pages from driving the serial port).
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

	// Exit when the window is closed (no event-stream client for 4 s), or if no window ever connected within 60 s.
	for {
		time.Sleep(500 * time.Millisecond)
		a.h.mu.Lock()
		n, ever, zero := len(a.h.subs), a.h.ever, a.h.zero
		a.h.mu.Unlock()
		if n == 0 && ((ever && time.Since(zero) > 4*time.Second) || (!ever && time.Since(a.start) > 60*time.Second)) {
			break
		}
	}
	a.closeAll()
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	_ = srv.Shutdown(ctx)
	_ = io.Discard
}
