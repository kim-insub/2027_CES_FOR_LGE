# PlatformIO post-build script: copy firmware.bin to bin/<FW_TYPE>_<DEVICE_ID>_v<FW_VERSION>.bin
#   e.g.  pio run -e dc27_frt_10   ->   bin/LED_0x10_v1.0.bin
# FW_TYPE / FW_VERSION come from src/config.h, DEVICE_ID from the environment's -DDEVICE_ID flag.
# This file is the one to send with tools/led_ota.exe (BLE wireless update).
Import("env")
import os, re, shutil

def _cfg(name):
    txt = open(os.path.join(env.subst("$PROJECT_SRC_DIR"), "config.h"), encoding="utf-8").read()
    m = re.search(r'#define\s+%s\s+"([^"]+)"' % name, txt)
    return m.group(1) if m else "X"

def _device_id():
    for d in env.get("CPPDEFINES", []):
        if isinstance(d, (list, tuple)) and d[0] == "DEVICE_ID":
            return "0x%02X" % int(str(d[1]), 0)
    return "0x10"

def copy_bin(source, target, env):
    out_dir = os.path.join(env.subst("$PROJECT_DIR"), "bin")
    os.makedirs(out_dir, exist_ok=True)
    name = "%s_%s_v%s.bin" % (_cfg("FW_TYPE"), _device_id(), _cfg("FW_VERSION"))
    shutil.copyfile(str(target[0]), os.path.join(out_dir, name))
    print("BLE update file: bin/" + name)

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", copy_bin)
