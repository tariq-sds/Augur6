{
  "patcher": {
    "fileversion": 1,
    "appversion": {
      "major": 8,
      "minor": 6,
      "revision": 5,
      "architecture": "x64",
      "modernui": 1
    },
    "classnamespace": "box",
    "rect": [
      80,
      100,
      1000,
      420
    ],
    "openrect": [
      0,
      0,
      420,
      169
    ],
    "openinpresentation": 1,
    "devicewidth": 420,
    "boxes": [
      {
        "box": {
          "id": "in",
          "maxclass": "newobj",
          "numinlets": 2,
          "numoutlets": 2,
          "patching_rect": [
            20,
            20,
            180,
            22
          ],
          "text": "plugin~",
          "outlettype": [
            "signal",
            "signal"
          ]
        }
      },
      {
        "box": {
          "id": "out",
          "maxclass": "newobj",
          "numinlets": 2,
          "numoutlets": 2,
          "patching_rect": [
            20,
            260,
            180,
            22
          ],
          "text": "plugout~",
          "outlettype": [
            "signal",
            "signal"
          ]
        }
      },
      {
        "box": {
          "id": "thisdev",
          "maxclass": "live.thisdevice",
          "numinlets": 1,
          "numoutlets": 3,
          "patching_rect": [
            200,
            20,
            180,
            22
          ],
          "outlettype": [
            "bang",
            "int",
            "int"
          ]
        }
      },
      {
        "box": {
          "id": "defer",
          "maxclass": "newobj",
          "numinlets": 1,
          "numoutlets": 1,
          "patching_rect": [
            200,
            50,
            180,
            22
          ],
          "text": "deferlow",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "init",
          "maxclass": "message",
          "numinlets": 2,
          "numoutlets": 1,
          "patching_rect": [
            200,
            80,
            180,
            22
          ],
          "text": "init",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "rx",
          "maxclass": "newobj",
          "numinlets": 1,
          "numoutlets": 1,
          "patching_rect": [
            400,
            20,
            180,
            22
          ],
          "text": "udpreceive 9137",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "rxdefer",
          "maxclass": "newobj",
          "numinlets": 1,
          "numoutlets": 1,
          "patching_rect": [
            400,
            50,
            180,
            22
          ],
          "text": "deferlow",
          "outlettype": [
            ""
          ]
        }
      },
      {
        "box": {
          "id": "engine",
          "maxclass": "newobj",
          "numinlets": 1,
          "numoutlets": 2,
          "patching_rect": [
            300,
            130,
            220,
            22
          ],
          "text": "js mirror-bridge.js",
          "outlettype": [
            "",
            ""
          ],
          "saved_object_attributes": {
            "filename": "mirror-bridge.js",
            "parameter_enable": 0
          }
        }
      },
      {
        "box": {
          "id": "tx",
          "maxclass": "newobj",
          "numinlets": 1,
          "numoutlets": 0,
          "patching_rect": [
            300,
            200,
            200,
            22
          ],
          "text": "udpsend 127.0.0.1 9138"
        }
      },
      {
        "box": {
          "id": "ui",
          "maxclass": "jsui",
          "numinlets": 1,
          "numoutlets": 1,
          "patching_rect": [
            560,
            130,
            420,
            22
          ],
          "outlettype": [
            ""
          ],
          "filename": "mirror-panel.js",
          "presentation": 1,
          "presentation_rect": [
            0,
            0,
            420,
            169
          ],
          "parameter_enable": 0
        }
      },
      {
        "box": {
          "id": "uidefer",
          "maxclass": "newobj",
          "numinlets": 1,
          "numoutlets": 1,
          "patching_rect": [
            560,
            320,
            180,
            22
          ],
          "text": "deferlow",
          "outlettype": [
            ""
          ]
        }
      }
    ],
    "lines": [
      {
        "patchline": {
          "source": [
            "in",
            0
          ],
          "destination": [
            "out",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "in",
            1
          ],
          "destination": [
            "out",
            1
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "thisdev",
            0
          ],
          "destination": [
            "defer",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "defer",
            0
          ],
          "destination": [
            "init",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "rx",
            0
          ],
          "destination": [
            "rxdefer",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "init",
            0
          ],
          "destination": [
            "engine",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "rxdefer",
            0
          ],
          "destination": [
            "engine",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "engine",
            0
          ],
          "destination": [
            "tx",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "engine",
            1
          ],
          "destination": [
            "ui",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "ui",
            0
          ],
          "destination": [
            "uidefer",
            0
          ]
        }
      },
      {
        "patchline": {
          "source": [
            "uidefer",
            0
          ],
          "destination": [
            "engine",
            0
          ]
        }
      }
    ],
    "dependency_cache": [
      {
        "name": "mirror-bridge.js",
        "type": "TEXT",
        "implicit": 1
      },
      {
        "name": "mirror-panel.js",
        "type": "TEXT",
        "implicit": 1
      }
    ],
    "autosave": 0
  }
}
