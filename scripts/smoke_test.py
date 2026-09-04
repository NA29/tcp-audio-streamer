#!/usr/bin/env python3
"""The original failure: a blocking server accepts client 1 and never reaches
accept() again. Proves 3 clients are served concurrently, out of connect order."""
import socket, struct, subprocess, sys, time

MAGIC = 0x41554449
PING = 3
def frame(t, body=b""): return struct.pack("!IHHI", MAGIC, t, 0, len(body)) + body

srv = subprocess.Popen(["./build/serverApp"], stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, text=True)
time.sleep(0.4)

conns = [socket.create_connection(("127.0.0.1", 12345)) for _ in range(3)]
print("3 clients connected")

# Send from client 3 FIRST: under the old blocking server, clients 2 and 3 were
# never accepted, so nothing they sent could ever be observed.
for idx in (2, 0, 1):
    conns[idx].sendall(frame(PING, f"client {idx+1}".encode()))
    time.sleep(0.15)

conns[1].close()          # one client leaves; the others must survive
time.sleep(0.2)
conns[0].sendall(frame(PING, b"still alive after a peer disconnected"))
time.sleep(0.3)

srv.terminate()
log = srv.stdout.read()
print(log)

connected = log.count("client connected")
frames    = log.count("frame type=")
# The departing client is dropped either as a clean EOF or as ECONNRESET -- closing
# a socket that still has unread data (our Pong) makes TCP send RST instead of FIN.
# Both are correct; assert the outcome, not the reason.
dropped   = log.count("client dropped")
ok = connected == 3 and frames == 4 and dropped == 1
print("---- result ----")
print(f"clients accepted: {connected}/3   frames handled: {frames}/4   dropped: {dropped}/1")
print("PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
