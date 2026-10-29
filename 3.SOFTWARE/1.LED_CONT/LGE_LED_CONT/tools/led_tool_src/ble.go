// BLE side of the LED tool: scan, connect (PIN), 1-byte host commands, firmware update (OTA) and the
// reconnect check after an update. Board side: src/ble_ota.h of the DC27_FRT firmware.
//
//	Board name : "<TYPE> 0x<ID> <DESC>"   e.g. "LED 0x10 DC27_FRT"  (advertised only while its USB port is out)
//	.bin tag   : "DC27FW|<TYPE>|0x<ID>|<VERSION>|"   (read from the file, checked again by the board)
//	GATT       : INFO (read tag) / CTRL (AUTH, BEGIN, END + results) / DATA (offset + payload, 4096 B blocks)
//	             HOST (same 1-byte commands as the COM port, answer by notification)
package main

import (
	"crypto/sha256"
	"encoding/binary"
	"encoding/json"
	"errors"
	"fmt"
	"net/http"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"tinygo.org/x/bluetooth"
)

var (
	uuidSvc  = bleUUID(1)
	uuidInfo = bleUUID(2)
	uuidCtrl = bleUUID(3)
	uuidData = bleUUID(4)
	uuidHost = bleUUID(5)

	// "LED 0x10 DC27_FRT", "RF 0x20 ...", ... (every DC27 board uses this name scheme)
	reName = regexp.MustCompile(`^([A-Z]+) 0x([0-9A-Fa-f]{2})(?: (.*))?$`)
	reTag  = regexp.MustCompile(`DC27FW\|([A-Z]+)\|0x([0-9A-Fa-f]{2})\|([\x20-\x7b\x7d\x7e]{1,16})\|`)
)

// 4c474501-dc27-4f54-a000-0000000000NN
func bleUUID(n byte) bluetooth.UUID {
	return bluetooth.NewUUID([16]byte{0x4c, 0x47, 0x45, 0x01, 0xdc, 0x27, 0x4f, 0x54, 0xa0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, n})
}

const blockSize = 4096

var statusText = map[byte]string{
	0: "OK", 1: "wrong PIN", 2: "locked after 3 wrong PINs (power-cycle the board)", 3: "file too big",
	4: "board is busy with another update", 5: "board could not prepare the flash", 6: "bad command", 7: "data order error",
	8: "flash write failed", 9: "update not started", 10: "size mismatch", 11: "SHA-256 mismatch (transfer error)",
	12: "image check failed (not an ESP32-C6 firmware)", 13: "no DC27FW tag in the image",
	14: "the .bin is for another board type", 15: "the .bin is for another board ID", 16: "could not switch the boot slot",
}

func stText(s byte) string {
	if t, ok := statusText[s]; ok {
		return t
	}
	return fmt.Sprintf("error %d", s)
}

func isBLE(port string) bool { return strings.HasPrefix(port, "BLE:") }

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

type notifyMsg struct {
	code byte
	st   byte
	off  uint32
}

// ---------------------------------------------------------------- state

type btState struct {
	a       *app
	adapter *bluetooth.Adapter
	err     error
	op      sync.Mutex // the adapter does one thing at a time: scan / connect / reconnect

	mu      sync.Mutex
	addrs   map[string]bluetooth.Address // "BLE:<addr>" -> address
	names   map[string]string            // "BLE:<addr>" -> advertised name
	bin     []byte
	binTag  fwTag
	binOK   bool
	lastDir string
}

func newBT(a *app) *btState {
	bt := &btState{a: a, adapter: bluetooth.DefaultAdapter, addrs: map[string]bluetooth.Address{}, names: map[string]string{}}
	bt.err = bt.adapter.Enable()
	if bt.err == nil {
		bt.adapter.SetConnectHandler(func(d bluetooth.Device, connected bool) {
			if !connected {
				go a.bleLost(d.Address.String())
			}
		})
	}
	return bt
}

type bleLink struct {
	port                   string
	addr                   string
	dev                    bluetooth.Device
	info, ctrl, data, host bluetooth.DeviceCharacteristic
	tag                    fwTag
	pin                    string
	ctrlCh                 chan notifyMsg
	hostCh                 chan byte
	ota, closed            atomic.Bool
}

func (l *bleLink) hostWrite(b []byte) error {
	_, err := l.host.Write(b)
	return err
}

func (l *bleLink) otaBusy() bool { return l.ota.Load() }

func (l *bleLink) close() {
	if l.closed.CompareAndSwap(false, true) {
		_ = l.dev.Disconnect()
	}
}

func (l *bleLink) waitCtrl(d time.Duration) (notifyMsg, error) {
	select {
	case m := <-l.ctrlCh:
		return m, nil
	case <-time.After(d):
		return notifyMsg{}, errors.New("no answer from the board (timeout)")
	}
}

// bleLost: Windows reports the link down (board rebooted, USB plugged back in, out of range).
func (a *app) bleLost(addr string) {
	a.mu.Lock()
	var port string
	for p, c := range a.conns {
		if c.ble != nil && c.ble.addr == addr && !c.ble.closed.Load() {
			port = p
		}
	}
	a.mu.Unlock()
	if port == "" {
		return
	}
	a.closeConn(port)
	a.logf("BLE link to %s closed by the board.", a.bleLabel(port))
	a.emit(map[string]any{"t": "closed", "port": port})
}

func (a *app) bleLabel(port string) string {
	a.bt.mu.Lock()
	defer a.bt.mu.Unlock()
	if n := a.bt.names[port]; n != "" {
		return n
	}
	return port
}

// ---------------------------------------------------------------- scan

func (a *app) bleScan(w http.ResponseWriter, r *http.Request) {
	bt := a.bt
	if bt.err != nil {
		writeJSON(w, 409, map[string]string{"error": "Bluetooth is not available on this PC: " + bt.err.Error()})
		return
	}
	if !bt.op.TryLock() {
		writeJSON(w, 409, map[string]string{"error": "Bluetooth is busy (connect / update in progress)"})
		return
	}
	go func() {
		defer bt.op.Unlock()
		a.logf("Searching BLE boards (6 s)... Boards advertise only while their USB port is out.")
		seen := map[string]bool{}
		t := time.AfterFunc(6*time.Second, func() { _ = bt.adapter.StopScan() })
		defer t.Stop()
		err := bt.adapter.Scan(func(_ *bluetooth.Adapter, res bluetooth.ScanResult) {
			name := res.LocalName()
			m := reName.FindStringSubmatch(name)
			if m == nil {
				return
			}
			port := "BLE:" + res.Address.String()
			bt.mu.Lock()
			bt.addrs[port] = res.Address
			bt.names[port] = name
			bt.mu.Unlock()
			if !seen[port] {
				seen[port] = true
				a.logf("  Found BLE board: %s  (%s, RSSI %d dBm)", name, res.Address.String(), res.RSSI)
			}
			a.emit(map[string]any{"t": "bledev", "port": port, "label": name, "type": m[1],
				"id": "0x" + strings.ToUpper(m[2]), "rssi": res.RSSI})
		})
		if err != nil {
			a.logf("BLE search error: %v", err)
		}
		if len(seen) == 0 {
			a.logf("No BLE board found. Unplug the board's USB (or switch the USB power off) and wait 2 s, then search again.")
		}
		a.emit(map[string]any{"t": "blescan_done", "n": len(seen)})
	}()
	writeJSON(w, 200, map[string]bool{"ok": true})
}

// scanFor waits until the address advertises again (after an update reboot). Caller holds bt.op.
func (bt *btState) scanFor(port string, d time.Duration) bool {
	bt.mu.Lock()
	want := bt.addrs[port].String()
	bt.mu.Unlock()
	found := false
	t := time.AfterFunc(d, func() { _ = bt.adapter.StopScan() })
	defer t.Stop()
	_ = bt.adapter.Scan(func(ad *bluetooth.Adapter, res bluetooth.ScanResult) {
		if res.Address.String() == want {
			found = true
			if name := res.LocalName(); name != "" {
				bt.mu.Lock()
				bt.addrs[port] = res.Address
				bt.names[port] = name
				bt.mu.Unlock()
			}
			_ = ad.StopScan()
		}
	})
	return found
}

// ---------------------------------------------------------------- connect = BLE version of probe()

// bleProbe connects, unlocks with the PIN and sends the ID request (0x00) over HOST, then reports the
// board like a COM port scan does. Returns the link on success.
func (a *app) bleProbe(port, pin string) *bleLink {
	bt := a.bt
	bt.op.Lock()
	defer bt.op.Unlock()
	return a.bleProbeLocked(port, pin)
}

func (a *app) bleProbeLocked(port, pin string) *bleLink {
	bt := a.bt
	a.closeConn(port)
	label := a.bleLabel(port)
	bt.mu.Lock()
	addr, ok := bt.addrs[port]
	bt.mu.Unlock()
	fail := func(msg string) *bleLink {
		a.logf("  %s: %s", label, msg)
		a.emit(map[string]any{"t": "device", "port": port, "label": label, "ble": true, "open": false, "err": msg, "pre": []int{}, "resp": []int{}})
		return nil
	}
	if !ok {
		return fail("unknown board, search again")
	}
	a.logf("Connecting BLE %s ...", label)
	dev, err := bt.adapter.Connect(addr, bluetooth.ConnectionParams{ConnectionTimeout: bluetooth.NewDuration(10 * time.Second)})
	if err != nil {
		return fail("connect failed: " + err.Error())
	}
	l := &bleLink{port: port, addr: addr.String(), dev: dev, pin: pin, ctrlCh: make(chan notifyMsg, 64), hostCh: make(chan byte, 256)}
	bad := func(msg string) *bleLink { l.close(); return fail(msg) }

	svcs, err := dev.DiscoverServices([]bluetooth.UUID{uuidSvc})
	if err != nil || len(svcs) == 0 {
		return bad("no DC27 service (firmware without BLE support)")
	}
	chars, err := svcs[0].DiscoverCharacteristics([]bluetooth.UUID{uuidInfo, uuidCtrl, uuidData, uuidHost})
	if err != nil || len(chars) != 4 {
		return bad("DC27 service incomplete (old firmware without BLE host commands? update it over USB once)")
	}
	for _, c := range chars {
		switch c.UUID() {
		case uuidInfo:
			l.info = c
		case uuidCtrl:
			l.ctrl = c
		case uuidData:
			l.data = c
		case uuidHost:
			l.host = c
		}
	}
	buf := make([]byte, 64)
	n, err := l.info.Read(buf)
	if err != nil {
		return bad("reading firmware info failed: " + err.Error())
	}
	if l.tag, ok = parseTag(buf[:n]); !ok {
		return bad(fmt.Sprintf("unknown firmware info %q", string(buf[:n])))
	}
	if err := l.ctrl.EnableNotifications(func(b []byte) {
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
		case l.ctrlCh <- m:
		default:
		}
	}); err != nil {
		return bad("notification setup failed: " + err.Error())
	}
	if err := l.host.EnableNotifications(func(b []byte) {
		for _, x := range b {
			select {
			case l.hostCh <- x:
			default:
			}
		}
	}); err != nil {
		return bad("notification setup failed: " + err.Error())
	}

	// AUTH with the PIN (unlocks HOST)
	if _, err := l.ctrl.Write(append([]byte{0x04, byte(len(pin))}, pin...)); err != nil {
		return bad("PIN send failed: " + err.Error())
	}
	m, err := l.waitCtrl(3 * time.Second)
	if err != nil {
		return bad(err.Error())
	}
	if m.code != 0x85 || m.st != 0 {
		return bad("PIN rejected: " + stText(m.st))
	}

	// ID request, same as the COM port probe: first byte = DEVICE_ID, anything else is reported as stray
	if err := l.hostWrite([]byte{0x00}); err != nil {
		return bad("ID request failed: " + err.Error())
	}
	var resp []int
	deadline := time.After(1500 * time.Millisecond)
collect:
	for {
		select {
		case b := <-l.hostCh:
			resp = append(resp, int(b))
			if len(resp) == 1 {
				deadline = time.After(300 * time.Millisecond) // short wait for extra bytes
			}
		case <-deadline:
			break collect
		}
	}
	if len(resp) == 0 {
		return bad("no ID answer over BLE")
	}

	c := &conn{ble: l, stop: make(chan struct{})}
	a.mu.Lock()
	a.conns[port] = c
	a.mu.Unlock()
	go func() {
		for {
			select {
			case <-c.stop:
				return
			case b := <-l.hostCh:
				a.emit(map[string]any{"t": "rx", "port": port, "b": int(b)})
			}
		}
	}()
	a.logf("  Found device: ID=0x%02X on %s (BLE, firmware %s %s v%s)", resp[0], label, l.tag.Type, l.tag.ID, l.tag.Ver)
	a.emit(map[string]any{"t": "device", "port": port, "label": label, "ble": true, "open": true, "pre": []int{}, "resp": resp,
		"ver": l.tag.Ver, "tag": l.tag})
	return l
}

// ---------------------------------------------------------------- .bin file

// browse: standard Windows "Open" dialog. Starts in the folder of the last file, else the project's
// bin\ folder next to tools\ (where the PlatformIO build puts LED_0x10_v1.0.bin), else the exe folder.
func (a *app) browse(w http.ResponseWriter, r *http.Request) {
	a.bt.mu.Lock()
	dir := a.bt.lastDir
	a.bt.mu.Unlock()
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
		writeJSON(w, 400, map[string]string{"error": "enter the .bin file path"})
		return
	}
	path := strings.Trim(strings.TrimSpace(req.Path), `"`)
	st, err := os.Stat(path)
	if err != nil || st.IsDir() {
		writeJSON(w, 400, map[string]string{"error": "file not found: " + path})
		return
	}
	if st.Size() == 0 || st.Size() > 4<<20 {
		writeJSON(w, 400, map[string]string{"error": "file size does not fit a firmware .bin"})
		return
	}
	data, err := os.ReadFile(path)
	if err != nil {
		writeJSON(w, 400, map[string]string{"error": "cannot read the file: " + err.Error()})
		return
	}
	tag, ok := parseTag(data)
	a.bt.mu.Lock()
	a.bt.bin, a.bt.binTag, a.bt.binOK = data, tag, ok
	a.bt.lastDir = filepath.Dir(path)
	a.bt.mu.Unlock()
	sum := sha256.Sum256(data)
	writeJSON(w, 200, map[string]any{"path": path, "name": filepath.Base(path), "size": len(data), "tagOK": ok,
		"tag": tag, "esp": data[0] == 0xE9, "sha": fmt.Sprintf("%X", sum[:4])})
}

// ---------------------------------------------------------------- firmware update

func (a *app) ota(w http.ResponseWriter, r *http.Request) {
	var req struct {
		Port  string `json:"port"`
		Force bool   `json:"force"`
	}
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil || req.Port == "" {
		writeJSON(w, 400, map[string]string{"error": "select a BLE board"})
		return
	}
	a.mu.Lock()
	c := a.conns[req.Port]
	a.mu.Unlock()
	a.bt.mu.Lock()
	bin, tag, binOK := a.bt.bin, a.bt.binTag, a.bt.binOK
	a.bt.mu.Unlock()
	switch {
	case c == nil || c.ble == nil:
		writeJSON(w, 400, map[string]string{"error": "connect the BLE board first (firmware update works over BLE only)"})
		return
	case bin == nil:
		writeJSON(w, 400, map[string]string{"error": "choose the .bin file first"})
		return
	case !binOK:
		writeJSON(w, 400, map[string]string{"error": "this .bin has no DC27FW tag (not a BLE-update firmware)"})
		return
	case c.ble.tag.Type != tag.Type:
		writeJSON(w, 400, map[string]string{"error": fmt.Sprintf("board type %s, file type %s", c.ble.tag.Type, tag.Type)})
		return
	case c.ble.tag.ID != tag.ID && !req.Force:
		writeJSON(w, 400, map[string]string{"error": fmt.Sprintf("board ID %s, file ID %s: tick 'Allow ID change' to change the ID", c.ble.tag.ID, tag.ID)})
		return
	}
	l := c.ble
	if !l.ota.CompareAndSwap(false, true) {
		writeJSON(w, 409, map[string]string{"error": "update already running"})
		return
	}
	go func() {
		label := a.bleLabel(req.Port)
		start := time.Now()
		a.logf("Firmware update %s: %s %s v%s -> %s %s v%s (%d bytes)", label, l.tag.Type, l.tag.ID, l.tag.Ver, tag.Type, tag.ID, tag.Ver, len(bin))
		c.wmu.Lock()
		err := a.upload(l, bin, req.Force)
		c.wmu.Unlock()
		l.ota.Store(false)
		if err != nil {
			a.logf("[ERROR] Update failed: %v (the board keeps its old firmware)", err)
			a.emit(map[string]any{"t": "ota_done", "port": req.Port, "ok": false, "m": err.Error()})
			return
		}
		a.logf("Update sent and checked by the board (%.1f s). The board reboots into the new firmware...", time.Since(start).Seconds())
		a.emit(map[string]any{"t": "ota_done", "port": req.Port, "ok": true})
		a.reconnectAfterUpdate(req.Port, l.pin, tag)
	}()
	writeJSON(w, 200, map[string]bool{"ok": true})
}

func (a *app) upload(l *bleLink, bin []byte, force bool) error {
	for len(l.ctrlCh) > 0 {
		<-l.ctrlCh
	}
	sum := sha256.Sum256(bin)
	cmd := []byte{0x01}
	cmd = binary.LittleEndian.AppendUint32(cmd, uint32(len(bin)))
	cmd = append(cmd, sum[:]...)
	flags := byte(0)
	if force {
		flags |= 1
	}
	cmd = append(cmd, flags, byte(len(l.pin)))
	cmd = append(cmd, l.pin...)
	if _, err := l.ctrl.Write(cmd); err != nil {
		return fmt.Errorf("start command failed: %w", err)
	}
	m, err := l.waitCtrl(10 * time.Second)
	if err != nil {
		return err
	}
	if m.code != 0x81 || m.st != 0 {
		return errors.New(stText(m.st))
	}

	// payload per packet: ATT MTU - 3 (write header) - 4 (offset), at most 240
	chunk := 240
	if mtu, err := l.data.GetMTU(); err == nil && int(mtu)-7 < chunk {
		chunk = int(mtu) - 7
	}
	if chunk < 16 {
		chunk = 16
	}
	pkt := make([]byte, 4+chunk)
	off, retries, lastPct := 0, 0, -1
	for off < len(bin) {
		end := min(off+blockSize, len(bin))
		for p := off; p < end; p += chunk {
			q := min(p+chunk, end)
			binary.LittleEndian.PutUint32(pkt, uint32(p))
			copy(pkt[4:], bin[p:q])
			if _, err := l.data.WriteWithoutResponse(pkt[:4+q-p]); err != nil {
				return fmt.Errorf("send failed at %d: %w", p, err)
			}
		}
		m, err := l.waitCtrl(8 * time.Second)
		if err != nil {
			return err
		}
		switch {
		case m.code == 0x82 && int(m.off) == end:
			off, retries = end, 0
		case m.code == 0x83 && m.st == 7 && retries < 3: // order error: resend the block
			retries++
			a.logf("  resending block at %d", m.off)
			off = int(m.off)
			time.Sleep(200 * time.Millisecond)
			for len(l.ctrlCh) > 0 {
				<-l.ctrlCh
			}
		case m.code == 0x83:
			return fmt.Errorf("%s (at %d)", stText(m.st), m.off)
		default:
			return fmt.Errorf("unexpected answer %02X %d", m.code, m.st)
		}
		if pct := off * 100 / len(bin); pct != lastPct {
			lastPct = pct
			a.emit(map[string]any{"t": "progress", "port": l.port, "pct": pct})
		}
	}

	if _, err := l.ctrl.Write([]byte{0x02}); err != nil {
		return fmt.Errorf("end command failed: %w", err)
	}
	m, err = l.waitCtrl(20 * time.Second)
	if err != nil {
		return err
	}
	if m.code != 0x84 || m.st != 0 {
		return errors.New(stText(m.st))
	}
	return nil
}

// reconnectAfterUpdate: the board reboots ~0.5 s after a good update and advertises again ~1-2 s later
// (its USB is still out). Reconnect, then compare what the board now reports with the .bin.
func (a *app) reconnectAfterUpdate(port, pin string, want fwTag) {
	time.Sleep(1500 * time.Millisecond)
	a.closeConn(port)
	a.emit(map[string]any{"t": "closed", "port": port})
	bt := a.bt
	for try := 1; try <= 8; try++ {
		time.Sleep(1500 * time.Millisecond)
		a.logf("Reconnecting after update (try %d/8)...", try)
		bt.op.Lock()
		var l *bleLink
		if bt.scanFor(port, 5*time.Second) {
			l = a.bleProbeLocked(port, pin)
		}
		bt.op.Unlock()
		if l == nil {
			continue
		}
		ok := l.tag.Type == want.Type && l.tag.ID == want.ID && l.tag.Ver == want.Ver
		if ok {
			a.logf("[CHECK] Update verified: board now runs %s %s v%s (same as the file). Continue testing with the effect commands.", l.tag.Type, l.tag.ID, l.tag.Ver)
		} else {
			a.logf("[CHECK] Board runs %s %s v%s, file was %s %s v%s -> NOT the new firmware (rolled back?)", l.tag.Type, l.tag.ID, l.tag.Ver, want.Type, want.ID, want.Ver)
		}
		a.emit(map[string]any{"t": "verify", "port": port, "ok": ok, "ver": l.tag.Ver})
		return
	}
	a.logf("[CHECK] Could not reconnect after the update. Check that the board's USB is still out, then press BLE SEARCH and Connect.")
	a.emit(map[string]any{"t": "verify", "port": port, "ok": false})
}
