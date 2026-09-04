#!/usr/bin/env python3
"""Real-time streaming: rate, continuity, multi-client fanout.

Both clients are read CONCURRENTLY via select, and each is drained before timing
starts -- otherwise bytes queued while we were not reading get counted inside the
measurement window and the observed rate looks too fast.
"""
import socket, struct, subprocess, sys, time, wave, os, math, select

MAGIC = 0x41554449
HELLO, AUDIO = 1, 2
def frame(t, body=b""): return struct.pack("!IHHI", MAGIC, t, 0, len(body)) + body

os.makedirs("assets", exist_ok=True)
path = "assets/test_tone.wav"
with wave.open(path, "wb") as w:
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(44100)
    data = bytearray()
    for i in range(44100 * 3):
        v = int(10000 * math.sin(2 * math.pi * 440 * i / 44100))
        data += struct.pack("<hh", v, v)
    w.writeframes(bytes(data))

srv = subprocess.Popen(["./build/serverApp", path], stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, text=True)
time.sleep(0.5)

class Reader:
    def __init__(self, sock):
        self.sock, self.buf = sock, b""
        self.frames = self.payload = 0
    def pump(self):
        chunk = self.sock.recv(65536)
        if not chunk: return
        self.buf += chunk
        while len(self.buf) >= 12:
            magic, ftype, _f, length = struct.unpack("!IHHI", self.buf[:12])
            assert magic == MAGIC, "framing desync"
            if len(self.buf) < 12 + length: break
            if ftype == AUDIO:
                self.frames += 1; self.payload += length
            self.buf = self.buf[12 + length:]
    def reset(self):
        self.frames = self.payload = 0

def run_for(readers, seconds):
    end = time.time() + seconds
    while time.time() < end:
        ready, _, _ = select.select([r.sock for r in readers], [], [], 0.05)
        for r in readers:
            if r.sock in ready: r.pump()

c1 = socket.create_connection(("127.0.0.1", 12345)); c1.sendall(frame(HELLO))
c2 = socket.create_connection(("127.0.0.1", 12345)); c2.sendall(frame(HELLO))
r1, r2 = Reader(c1), Reader(c2)

run_for([r1, r2], 0.5)          # drain startup backlog
r1.reset(); r2.reset()          # ...and discard it

t0 = time.time()
run_for([r1, r2], 2.0)
elapsed = time.time() - t0

srv.terminate()
log = srv.stdout.read()

BYTES_PER_SEC = 44100 * 2 * 2
rate1 = r1.payload / elapsed
rate2 = r2.payload / elapsed
drift1 = abs(rate1 - BYTES_PER_SEC) / BYTES_PER_SEC
drift2 = abs(rate2 - BYTES_PER_SEC) / BYTES_PER_SEC
expected_frames = elapsed / 0.020

print(log[:400])
print("---- result ----")
print(f"window {elapsed:.2f}s, expected ~{expected_frames:.0f} frames @20ms tick")
print(f"client1: {r1.frames} frames  {rate1:,.0f} B/s  (drift {drift1*100:.1f}%)")
print(f"client2: {r2.frames} frames  {rate2:,.0f} B/s  (drift {drift2*100:.1f}%)")
print(f"expected rate: {BYTES_PER_SEC:,} B/s")
print(f"fell behind: {'yes' if 'fell behind' in log else 'no'}")

ok = drift1 < 0.05 and drift2 < 0.05 and "44100 Hz" in log and "fell behind" not in log
print("PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
