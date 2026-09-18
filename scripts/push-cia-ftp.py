#!/usr/bin/env python3
import sys
import os
import time
import socket
import ftplib
from pathlib import Path

# Bind all socket connections to wlan0 to bypass cellular/USB-tether default routes
orig_create_connection = socket.create_connection
def bound_create_connection(address, timeout=socket._GLOBAL_DEFAULT_TIMEOUT, source_address=None):
    s = socket.socket()
    try:
        s.setsockopt(socket.SOL_SOCKET, 25, b'wlan0\0')
    except Exception:
        pass
    if timeout is not socket._GLOBAL_DEFAULT_TIMEOUT:
        s.settimeout(timeout)
    s.connect(address)
    return s

socket.create_connection = bound_create_connection

TARGET_IP = os.environ.get("3DS_IP", "192.168.4.24")
TARGET_PORT = int(os.environ.get("3DS_PORT", "5000"))
CIA_PATH = Path(sys.argv[1] if len(sys.argv) > 1 else "builds/soh_3ds-n3ds.cia").resolve()

if not CIA_PATH.exists():
    print(f"Error: CIA file {CIA_PATH} does not exist!")
    sys.exit(1)

file_size = CIA_PATH.stat().st_size
print(f"Target CIA: {CIA_PATH.name} ({file_size / (1024*1024):.2f} MB)")
print(f"Connecting to 3DS at {TARGET_IP}:{TARGET_PORT}...")

ftp = ftplib.FTP()
ftp.connect(TARGET_IP, TARGET_PORT, timeout=10)
ftp.login()
print("Connected!")

# Navigate to /cias
try:
    ftp.cwd("/cias")
except Exception:
    ftp.mkd("/cias")
    ftp.cwd("/cias")

uploaded_bytes = 0
start_upload = time.time()
last_print_time = 0

def callback(chunk):
    global uploaded_bytes, last_print_time
    uploaded_bytes += len(chunk)
    now = time.time()
    if now - last_print_time >= 0.5 or uploaded_bytes == file_size:
        percent = (uploaded_bytes / file_size) * 100
        mb_up = uploaded_bytes / (1024 * 1024)
        mb_tot = file_size / (1024 * 1024)
        speed = mb_up / max(0.01, now - start_upload)
        sys.stdout.write(f"\rUploading: {percent:5.1f}% [{mb_up:5.1f} / {mb_tot:5.1f} MB] @ {speed:4.2f} MB/s")
        sys.stdout.flush()
        last_print_time = now

print(f"Uploading to /cias/{CIA_PATH.name}...")
with open(CIA_PATH, "rb") as f:
    ftp.storbinary(f"STOR {CIA_PATH.name}", f, blocksize=65536, callback=callback)

ftp.quit()
total_time = time.time() - start_upload
print(f"\nUpload complete in {total_time:.1f}s ({file_size / (1024*1024) / max(0.01, total_time):.2f} MB/s)")
print(f"Installed to /cias/{CIA_PATH.name} on your 3DS SD card. Ready to install in FBI.")
