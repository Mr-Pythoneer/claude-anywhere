#!/usr/bin/env python3
"""Generate Claude Anywhere USB drive FAT12 disk image with LFN support.
Run:  python3 tools/make_disk.py > disk_image.h
"""
import struct, sys

SECTOR        = 512
TOTAL_SECTORS = 128          # 64 KB total
FAT_COUNT     = 2
FAT_SECTORS   = 1
ROOT_ENTRIES  = 32           # 32 x 32 bytes = 2 sectors
ROOT_SECTORS  = ROOT_ENTRIES * 32 // SECTOR
RESERVED      = 1
DATA_OFFSET   = RESERVED + FAT_COUNT * FAT_SECTORS + ROOT_SECTORS  # sector 5

disk = bytearray(TOTAL_SECTORS * SECTOR)
fat  = bytearray(FAT_SECTORS * SECTOR)
root = bytearray(ROOT_SECTORS * SECTOR)

root_pos     = 0   # next free root directory entry index
next_cluster = 2   # clusters 0 and 1 are reserved

# ── Boot sector ────────────────────────────────────────────────────────
def make_boot():
    b = bytearray(SECTOR)
    b[0:3]  = bytes([0xEB, 0x3C, 0x90])
    b[3:11] = b'CLAUDEAW'
    struct.pack_into('<H', b, 11, SECTOR)
    b[13]   = 1
    struct.pack_into('<H', b, 14, RESERVED)
    b[16]   = FAT_COUNT
    struct.pack_into('<H', b, 17, ROOT_ENTRIES)
    struct.pack_into('<H', b, 19, TOTAL_SECTORS)
    b[21]   = 0xF8
    struct.pack_into('<H', b, 22, FAT_SECTORS)
    struct.pack_into('<H', b, 24, 1)
    struct.pack_into('<H', b, 26, 1)
    b[36]   = 0x80
    b[38]   = 0x29
    struct.pack_into('<I', b, 39, 0xCB0D1234)
    b[43:54] = b'CLAUDE ANY '             # volume label: exactly 11 bytes
    b[54:62] = b'FAT12   '
    b[510:512] = bytes([0x55, 0xAA])
    return b

disk[0:SECTOR] = make_boot()

# ── FAT12 helpers ──────────────────────────────────────────────────────
fat[0] = 0xF8; fat[1] = 0xFF; fat[2] = 0xFF  # reserved clusters 0 and 1

def fat_set(cluster, value):
    offset = cluster + cluster // 2
    if cluster % 2 == 0:
        fat[offset]     =  value        & 0xFF
        fat[offset + 1] = (fat[offset+1] & 0xF0) | ((value >> 8) & 0x0F)
    else:
        fat[offset]     = (fat[offset]   & 0x0F) | ((value & 0x0F) << 4)
        fat[offset + 1] =  (value >> 4)  & 0xFF

def cluster_offset(cluster):
    return (DATA_OFFSET + cluster - 2) * SECTOR

# ── LFN helpers ────────────────────────────────────────────────────────
def lfn_checksum(name83):
    s = 0
    for byte in name83:
        s = (((s & 1) << 7) + (s >> 1) + byte) & 0xFF
    return s

def make_lfn_entry(seq, is_last, chunk13, cksum):
    """chunk13: list of exactly 13 UTF-16 code units (int)."""
    e = bytearray(32)
    e[0]  = (seq & 0x1F) | (0x40 if is_last else 0)
    e[11] = 0x0F   # LFN attribute marker
    e[13] = cksum
    # Name slots: offsets 1 (5 chars), 14 (6 chars), 28 (2 chars)
    slots = [(1, 5), (14, 6), (28, 2)]
    ci = 0
    for off, count in slots:
        for j in range(count):
            v = chunk13[ci] if ci < len(chunk13) else 0xFFFF
            struct.pack_into('<H', e, off + j * 2, v)
            ci += 1
    return bytes(e)

def make_dirent(name83_bytes, start_cluster, file_size, attr=0x21):
    e = bytearray(32)
    e[0:11] = name83_bytes
    e[11]   = attr
    # Date: 2026-01-01 = year-1980=46, month=1, day=1
    date = (46 << 9) | (1 << 5) | 1
    struct.pack_into('<H', e, 22, 0)     # time
    struct.pack_into('<H', e, 24, date)  # write date
    struct.pack_into('<H', e, 18, date)  # access date
    struct.pack_into('<H', e, 26, start_cluster)
    struct.pack_into('<I', e, 28, file_size)
    return bytes(e)

# ── Add file ───────────────────────────────────────────────────────────
def add_file(name83_str, long_name, content):
    """
    name83_str: exactly 11-char string like 'README  TXT' or 'LCHMAC  SH '
    long_name:  display name like 'README.txt' or 'Launch Mac.sh' (None = skip LFN)
    content:    bytes or str
    """
    global root_pos, next_cluster

    name83 = name83_str.encode('ascii')
    assert len(name83) == 11

    if isinstance(content, str):
        content = content.encode('utf-8')

    cksum = lfn_checksum(name83)

    # Write LFN entries if long_name given
    if long_name:
        utf16 = [ord(c) for c in long_name] + [0x0000]
        num_lfn = (len(utf16) + 12) // 13
        while len(utf16) < num_lfn * 13:
            utf16.append(0xFFFF)

        # LFN entries stored in reverse sequence order (highest seq first)
        for seq in range(num_lfn, 0, -1):
            chunk = utf16[(seq - 1) * 13 : seq * 13]
            entry = make_lfn_entry(seq, seq == num_lfn, chunk, cksum)
            off = root_pos * 32
            root[off:off + 32] = entry
            root_pos += 1

    # Allocate clusters
    if len(content) == 0:
        first_cluster = 0
    else:
        num_clusters  = (len(content) + SECTOR - 1) // SECTOR
        first_cluster = next_cluster
        # Write data
        byte_off = cluster_offset(first_cluster)
        disk[byte_off:byte_off + len(content)] = content
        # Chain FAT entries
        for i in range(num_clusters - 1):
            fat_set(first_cluster + i, first_cluster + i + 1)
        fat_set(first_cluster + num_clusters - 1, 0xFFF)  # end of chain
        next_cluster += num_clusters

    # Write regular 8.3 directory entry
    entry = make_dirent(name83, first_cluster, len(content))
    off = root_pos * 32
    root[off:off + 32] = entry
    root_pos += 1

# ── File contents ──────────────────────────────────────────────────────
README = b"""\
CLAUDE BUDDY - Portable AI Assistant
=====================================

Plug this device into any computer that is on the same WiFi network.

QUICK START:
  Mac:     Open "Launch Mac.sh" in Terminal
  Windows: Double-click "Launch Windows.bat"
  Linux:   Run "Launch Linux.sh" in Terminal
  Any OS:  Open "Start Here.htm" in a browser
           then go to http://claudeanywhere.local

REQUIREMENTS:
  - Python 3 (for companion + code execution)
      Mac:     brew install python3
      Linux:   sudo apt install python3
      Windows: python.org/downloads  (check "Add Python to PATH")
  - Device and computer must be on the SAME WiFi network

WiFi SETUP:
  If LED is orange, connect your phone/laptop to the hotspot
  "Claude-Buddy-Setup" and open http://192.168.4.1

RASPBERRY PI / HEADLESS LINUX:
  Serial terminal: screen /dev/ttyUSB0 115200
                   screen /dev/ttyACM0 115200
  Or open http://claudeanywhere.local in any browser.

LED GUIDE:
  Orange  = connecting to WiFi / hotspot active
  Blue    = ready and waiting
  Rainbow = thinking (Claude is replying)
  Green   = reply done
  Red     = error

BUTTON:
  Tap     = clear conversation
  Hold 3s = reset WiFi settings

https://github.com/Mr-Pythoneer/claude-anywhere
"""

START_HTM = b"""\
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Claude Anywhere - Start Here</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:#0d1117;color:#e6edf3;max-width:620px;margin:40px auto;padding:20px}
h1{font-size:1.5rem;color:#58a6ff;margin-bottom:8px}
p{color:#8b949e;margin:10px 0}
.btn{display:block;padding:16px 24px;margin:16px 0;background:#238636;color:#fff;text-decoration:none;text-align:center;border-radius:10px;font-size:1.1rem;font-weight:600;border:none;cursor:pointer}
.btn:hover{background:#2ea043}
code{background:#161b22;padding:2px 8px;border-radius:4px;font-size:.9rem;color:#79c0ff}
h2{margin-top:24px;color:#e6edf3;font-size:1rem}
ul{padding-left:20px;color:#8b949e;line-height:1.8}
hr{border:none;border-top:1px solid #30363d;margin:20px 0}
</style>
</head>
<body>
<h1>Claude Anywhere</h1>
<p>Your portable AI device is ready.</p>
<a class="btn" href="http://claudeanywhere.local">Open Chat &rarr; claudeanywhere.local</a>
<hr>
<h2>Companion (enables code execution)</h2>
<ul>
<li><strong>Mac/Linux:</strong> run <code>Launch Mac.sh</code> or <code>Launch Linux.sh</code> in Terminal</li>
<li><strong>Windows:</strong> double-click <code>Launch Windows.bat</code></li>
</ul>
<p>Requires <a href="https://python.org/downloads" style="color:#58a6ff">Python 3</a>.</p>
<hr>
<p style="font-size:.8rem;color:#484f58">LED: orange=connecting &bull; blue=ready &bull; rainbow=thinking &bull; green=done &nbsp;&bull;&nbsp; <a href="https://github.com/Mr-Pythoneer/claude-anywhere" style="color:#484f58">GitHub</a></p>
</body>
</html>
"""

MAC_SH = b"""\
#!/bin/bash
clear
echo "=== Claude Anywhere ==="
echo ""
if ! command -v python3 &>/dev/null; then
  echo "ERROR: Python 3 not found."
  echo ""
  echo "Install options:"
  echo "  brew install python3"
  echo "  xcode-select --install"
  echo ""
  read -rp "Press Enter to close..."
  exit 1
fi
echo "Starting companion..."
echo ""
curl -s http://claudeanywhere.local/run | python3
if [ ${PIPESTATUS[0]} -ne 0 ]; then
  echo ""
  echo "Could not reach device. Check:"
  echo "  1. Device LED is blue (not orange)"
  echo "  2. Your Mac is on the same WiFi as the device"
  echo "  3. Try opening http://claudeanywhere.local in a browser"
  echo ""
  read -rp "Press Enter to close..."
fi
"""

WIN_BAT = b"""\
@echo off
echo === Claude Anywhere ===
echo.
where python >nul 2>nul
if errorlevel 1 (
  echo ERROR: Python 3 not found.
  echo.
  echo Download from: https://www.python.org/downloads/
  echo IMPORTANT: check "Add Python to PATH" during install.
  echo.
  pause
  exit /b 1
)
echo Starting companion...
echo.
curl -s http://claudeanywhere.local/run | python
if errorlevel 1 (
  echo.
  echo Could not reach device. Check:
  echo   1. Device LED is blue (not orange)
  echo   2. This PC is on the same WiFi as the device
  echo   3. Try: http://claudeanywhere.local in a browser
  echo.
  pause
)
"""

LINUX_SH = b"""\
#!/bin/bash
clear
echo "=== Claude Anywhere ==="
echo ""
if ! command -v python3 &>/dev/null; then
  echo "ERROR: Python 3 not found."
  echo "  Install: sudo apt install python3"
  echo ""
  read -rp "Press Enter to close..."
  exit 1
fi
echo "Starting companion..."
echo ""
curl -s http://claudeanywhere.local/run | python3
if [ ${PIPESTATUS[0]} -ne 0 ]; then
  echo ""
  echo "Could not reach device. Check:"
  echo "  1. Device LED is blue (not orange)"
  echo "  2. On the same WiFi as the device"
  echo "  3. Try: http://claudeanywhere.local in browser"
  echo "  Raspberry Pi: screen /dev/ttyUSB0 115200"
  echo ""
  read -rp "Press Enter to close..."
fi
"""

# ── Build the image ────────────────────────────────────────────────────
# Volume label entry (no LFN, attribute 0x08)
vol = bytearray(32)
vol[0:11] = b'CLAUDE ANY '             # 11 bytes exactly
vol[11]   = 0x08
root[0:32] = vol
root_pos   = 1

add_file('README  TXT', None,              README)
add_file('START   HTM', 'Start Here.htm', START_HTM)
add_file('LCHMAC  SH ', 'Launch Mac.sh',  MAC_SH)
add_file('LCHWIN  BAT', 'Launch Windows.bat', WIN_BAT)
add_file('LCHLNX  SH ', 'Launch Linux.sh', LINUX_SH)

# ── Assemble disk ──────────────────────────────────────────────────────
fat_off  = RESERVED * SECTOR
root_off = (RESERVED + FAT_COUNT * FAT_SECTORS) * SECTOR

disk[fat_off           : fat_off + len(fat)]   = fat   # FAT1
disk[fat_off + SECTOR  : fat_off + SECTOR + len(fat)] = fat  # FAT2
disk[root_off          : root_off + len(root)]  = root

# ── Verify size ───────────────────────────────────────────────────────
assert len(disk) == TOTAL_SECTORS * SECTOR, f"disk is {len(disk)} bytes, expected {TOTAL_SECTORS*SECTOR}"

# ── Output ────────────────────────────────────────────────────────────
import os
if '--binary' in sys.argv:
    # Write binary to disk.bin and C header to stdout
    bin_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'disk.bin')
    with open(bin_path, 'wb') as f:
        f.write(disk)
    print(f"Wrote {len(disk)} bytes to {bin_path}", file=sys.stderr)
    sys.exit(0)

print("// Auto-generated by tools/make_disk.py — do not edit")
print("#pragma once")
print(f"#define DISK_SECTOR_COUNT {TOTAL_SECTORS}")
print(f"#define DISK_SECTOR_SIZE  {SECTOR}")
print(f"static const uint8_t DISK_IMAGE[{TOTAL_SECTORS * SECTOR}] PROGMEM = {{")
for i in range(0, len(disk), 16):
    chunk = disk[i:i+16]
    line  = ', '.join(f'0x{b:02x}' for b in chunk)
    end   = ',' if i + 16 < len(disk) else ''
    print(f'  {line}{end}')
print("};")
