#!/usr/bin/env python3
"""
Builds LapLogic-Flasher.html: one self-contained page with both firmware
images and the flashing library inlined, so it works when opened straight
from an unzipped folder with no internet.

Run after rebuilding the firmware:
    python3 tools/build_flasher.py

Reads:   firmware/LapLogic-*.bin, LapLogic/config.h (version), LapLogic/settings.cpp (time zones),
         tools/flasher.template.html, tools/esptool.bundle.js, tools/web/form.css + form.js
Writes:  LapLogic-Flasher.html (in the package root)
"""
import base64
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
TOOLS = ROOT / "tools"


def b64_lines(data: bytes) -> str:
    s = base64.b64encode(data).decode("ascii")
    return "\n".join(s[i:i + 120] for i in range(0, len(s), 120))


def main() -> int:
    version = re.search(r'#define\s+APP_VERSION\s+"([^"]+)"', (ROOT / "LapLogic/config.h").read_text()).group(1)
    fw_cyd = (ROOT / "firmware/LapLogic-CYD-ESP32-2432S028R.bin").read_bytes()
    fw_jc = (ROOT / "firmware/LapLogic-Guition-JC4827W543.bin").read_bytes()
    lib = (TOOLS / "esptool.bundle.js").read_text()
    if "</script" in lib.lower():
        lib = re.sub(r"</(script)", r"<\\/\1", lib, flags=re.I)

    html = (TOOLS / "flasher.template.html").read_text()
    for key, val in (("__VERSION__", version), ("__FW_CYD__", b64_lines(fw_cyd)), ("__FW_JC__", b64_lines(fw_jc))):
        if key not in html:
            print(f"template is missing {key}", file=sys.stderr)
            return 1
        html = html.replace(key, val)
    html = html.replace("/*ESPTOOL*/", lib, 1)

    # the shared settings form (also used by the board's own page) and the time zone list
    web = TOOLS / "web"
    html = html.replace("/*FORM_CSS*/", (web / "form.css").read_text(), 1)
    html = html.replace("/*FORM_JS*/", (web / "form.js").read_text(), 1)
    tz = re.findall(r'^\s*\{"([A-Za-z_]+/[A-Za-z_/+-]+|UTC)",', (ROOT / "LapLogic/settings.cpp").read_text(), flags=re.M)
    if not tz:
        print("couldn't read the time zone list from settings.cpp", file=sys.stderr)
        return 1
    html = html.replace("__TZ_LIST__", json.dumps(tz), 1)

    out = ROOT / "LapLogic-Flasher.html"
    out.write_text(html)
    print(f"Wrote {out.name}: {version}, {out.stat().st_size / 1048576:.1f} MB "
          f"(CYD {len(fw_cyd):,} bytes, Guition {len(fw_jc):,} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
