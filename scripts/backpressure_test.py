#!/usr/bin/env python3
"""A client that refuses to read must not affect anyone else, and must not grow
the server's memory without bound."""
import socket, struct, subprocess, sys, time

MAGIC = 0x41554449
HELLO, AUDIO, PING, PONG = 1, 2, 3, 4
def frame(t, body=b""): return struct.pack("!IHHI", MAGIC, t, 0, len(body)) + body

srv = subprocess.Popen(["./build/serverApp"], stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, text=True)
time.sleep(0.4)

# --- the slow consumer: asks for the stream, then never reads a byte ---
slow = socket.create_connection(("127.0.0.1", 12345))
slow.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)  # tiny receive window
slow.sendall(frame(HELLO, b"gimme audio"))

# --- the fast client: must keep getting timely responses throughout ---
fast = socket.create_connection(("127.0.0.1", 12345))
fast.settimeout(2.0)

latencies = []
for i in range(30):
    t0 = time.perf_counter()
    fast.sendall(frame(PING))
    hdr = b""
    while len(hdr) < 12:
        hdr += fast.recv(12 - len(hdr))
    magic, ftype, _flags, length = struct.unpack("!IHHI", hdr)
    latencies.append((time.perf_counter() - t0) * 1000)
    assert ftype == PONG, f"expected PONG, got {ftype}"
    time.sleep(0.1)

time.sleep(0.5)
srv.terminate()
log = srv.stdout.read()
print(log[-1500:])

worst = max(latencies)
slow_dropped = "slow consumer" in log
fast_ok = len(latencies) == 30 and worst < 250
print("---- result ----")
print(f"fast-client ping latencies (ms): {[round(x,2) for x in latencies]}")
print(f"worst: {worst:.2f}ms   slow consumer dropped: {slow_dropped}")
print("PASS" if (slow_dropped and fast_ok) else "FAIL")
sys.exit(0 if (slow_dropped and fast_ok) else 1)
