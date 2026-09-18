#!/usr/bin/env python3
import sys
import os
import time
import socket
import ftplib
from pathlib import Path

TARGET_IP = os.environ.get("3DS_IP", "192.168.1.25")
TARGET_PORT = int(os.environ.get("3DS_PORT", "5000"))
CIA_PATH = Path(sys.argv[1] if len(sys.argv) > 1 else "builds/soh_3ds-n3ds.cia").resolve()

if not CIA_PATH.exists():
    print(f"Error: CIA file {CIA_PATH} does not exist!")
    sys.exit(1)

file_size = CIA_PATH.stat().st_size
print(f"Target CIA: {CIA_PATH} ({file_size / (1024*1024):.2f} MB)")
print(f"Connecting to 3DS at {TARGET_IP}:{TARGET_PORT}...")

# Wait for FTP port to open if needed
start_wait = time.time()
while True:
    try:
        s = socket.socket()
        s.settimeout(1.0)
        s.connect((TARGET_IP, TARGET_PORT))
        s.close()
        break
    except Exception:
        sys.stdout.write(f"\rWaiting for FTPD to start on 3DS ({TARGET_IP}:{TARGET_PORT})... [{int(time.time() - start_wait)}s]")
        sys.stdout.flush()
        time.sleep(1.0)

print(f"\nConnected to 3DS! Starting upload...")
ftp = ftplib.FTP()
ftp.connect(TARGET_IP, TARGET_PORT, timeout=10)
ftp.login()

# Check/create /cias folder
try:
    ftp.cwd("/cias")
except Exception:
    try:
        ftp.mkd("/cias")
        ftp.cwd("/cias")
    except Exception:
        ftp.cwd("/")

uploaded_bytes = 0
last_print_time = time.time()

def callback(chunk):
    global uploaded_bytes, last_print_time
    uploaded_bytes += len(chunk)
    now = time.time()
    if now - last_print_time >= 0.5 or uploaded_bytes == file_size:
        percent = (uploaded_bytes / file_size) * 100
        mb_up = uploaded_bytes / (1024 * 1024)
        mb_tot = file_size / (1024 * 1024)
        speed = (uploaded_bytes / (1024 * 1024)) / max(0.1, now - start_upload)
        sys.stdout.write(f"\rUploading: {percent:.1f}% ({mb_up:.1f}/{mb_tot:.1f} MB) - {speed:.2f} MB/s")
        sys.stdout.flush()
        last_print_time = now

target_filename = CIA_PATH.name
print(f"Uploading to /cias/{target_filename}...")
start_upload = time.time()
with open(CIA_PATH, "rb") as f:
    ftp.storbinary(f"STOR {target_filename}", f, blocksize=65536, callback=callback)

ftp.quit()
total_time = time.time() - start_upload
print(f"\nUpload complete in {total_time:.1f}s! ({file_size / (1024*1024) / max(0.1, total_time):.2f} MB/s)")
print("You can now install the CIA using FBI on your 3DS.")
