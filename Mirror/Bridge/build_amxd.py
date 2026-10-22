"""Builds Mirror-Bridge.amxd (Max for Live audio effect) from the two JS files next to it."""
import json, struct
from pathlib import Path

here = Path(__file__).resolve().parent
boxes, lines = [], []

def box(id, maxclass, text=None, x=0, y=0, w=180, ins=1, outs=1, outtype=None, **kw):
    b = {"id": id, "maxclass": maxclass, "numinlets": ins, "numoutlets": outs, "patching_rect": [x, y, w, 22]}
    if text is not None:
        b["text"] = text
    if outtype is not None:
        b["outlettype"] = outtype
    b.update(kw)
    boxes.append({"box": b})

def wire(a, o, b, i=0):
    lines.append({"patchline": {"source": [a, o], "destination": [b, i]}})

# audio passes straight through (the device only talks to Live's API)
box("in", "newobj", "plugin~", 20, 20, ins=2, outs=2, outtype=["signal", "signal"])
box("out", "newobj", "plugout~", 20, 260, ins=2, outs=2, outtype=["signal", "signal"])
wire("in", 0, "out", 0); wire("in", 1, "out", 1)

# start-up: wait for Live, then init
box("thisdev", "live.thisdevice", None, 200, 20, ins=1, outs=3, outtype=["bang", "int", "int"])
box("defer", "newobj", "deferlow", 200, 50, outtype=[""])
box("init", "message", "init", 200, 80, ins=2, outs=1, outtype=[""])
wire("thisdev", 0, "defer"); wire("defer", 0, "init")

# OSC from Mirror -> engine (deferred so Live API calls run on the low-priority thread)
box("rx", "newobj", "udpreceive 9137", 400, 20, outtype=[""])
box("rxdefer", "newobj", "deferlow", 400, 50, outtype=[""])
wire("rx", 0, "rxdefer")

box("engine", "newobj", "js mirror-bridge.js", 300, 130, w=220, ins=1, outs=2, outtype=["", ""], saved_object_attributes={"filename": "mirror-bridge.js", "parameter_enable": 0})
wire("init", 0, "engine"); wire("rxdefer", 0, "engine")

# engine -> Mirror
box("tx", "newobj", "udpsend 127.0.0.1 9138", 300, 200, w=200, ins=1, outs=0)
wire("engine", 0, "tx")

# panel
box("ui", "jsui", None, 560, 130, w=420, ins=1, outs=1, outtype=[""], filename="mirror-panel.js",
    presentation=1, presentation_rect=[0, 0, 420, 169], parameter_enable=0)
box("uidefer", "newobj", "deferlow", 560, 320, outtype=[""])
wire("engine", 1, "ui"); wire("ui", 0, "uidefer"); wire("uidefer", 0, "engine")

patcher = {
    "patcher": {
        "fileversion": 1,
        "appversion": {"major": 8, "minor": 6, "revision": 5, "architecture": "x64", "modernui": 1},
        "classnamespace": "box",
        "rect": [80, 100, 1000, 420],
        "openrect": [0, 0, 420, 169],
        "openinpresentation": 1,
        "devicewidth": 420,
        "boxes": boxes,
        "lines": lines,
        "dependency_cache": [
            {"name": "mirror-bridge.js", "type": "TEXT", "implicit": 1},
            {"name": "mirror-panel.js", "type": "TEXT", "implicit": 1},
        ],
        "autosave": 0,
    }
}

j = json.dumps(patcher, indent=2).encode() + b"\n"
(here / "Mirror-Bridge.maxpat").write_bytes(j)
payload = j + b"\0"
amxd = b"ampf" + struct.pack("<I", 4) + b"aaaa" + b"meta" + struct.pack("<II", 4, 1) + b"ptch" + struct.pack("<I", len(payload)) + payload
(here / "Mirror-Bridge.amxd").write_bytes(amxd)
print("built", len(amxd), "bytes")
