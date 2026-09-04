#!/usr/bin/env python3
"""End-to-end: real sockets, fragmented writes, malformed input."""
import socket, struct, subprocess, sys, time

MAGIC = 0x41554449
def frame(ftype, body: bytes) -> bytes:
    return struct.pack("!IHHI", MAGIC, ftype, 0, len(body)) + body

srv = subprocess.Popen(["./build/serverApp"], stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, text=True)
time.sleep(0.4)

# --- client A: one frame split across three writes, with delays ---
a = socket.create_connection(("127.0.0.1", 12345))
blob = frame(1, b"split across three writes")
a.sendall(blob[:5]);  time.sleep(0.15)     # partial header
a.sendall(blob[5:14]); time.sleep(0.15)    # rest of header + start of payload
a.sendall(blob[14:]);  time.sleep(0.2)     # remainder

# --- client B: three frames in a single write ---
b = socket.create_connection(("127.0.0.1", 12345))
b.sendall(frame(1, b"one") + frame(3, b"") + frame(2, b"three"))
time.sleep(0.25)

# --- client C: garbage magic -> must be dropped, others must survive ---
c = socket.create_connection(("127.0.0.1", 12345))
c.sendall(b"\xde\xad\xbe\xef" + b"\x00" * 20)
time.sleep(0.25)

# A and B must still work after C was killed
a.sendall(frame(1, b"still here")); time.sleep(0.3)

srv.terminate()
log = srv.stdout.read()
print(log)

frames   = log.count("frame type=")
dropped  = log.count("stream desynchronized")
survived = "payload=10 bytes" in log
ok = frames == 5 and dropped == 1 and survived
print("---- result ----")
print(f"frames parsed: {frames}/5   malformed clients dropped: {dropped}/1   "
      f"survivors still served: {survived}")
print("PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
