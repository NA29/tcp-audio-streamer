#!/usr/bin/env python3
"""Proves the old failure is fixed: 3 clients connected at once, served out of order."""
import socket, subprocess, sys, time

srv = subprocess.Popen(["./build/serverApp"], stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, text=True)
time.sleep(0.4)

conns = []
for i in range(3):
    s = socket.create_connection(("127.0.0.1", 12345))
    conns.append(s)
    print(f"client {i+1} connected")

# Send from client 3 FIRST. Under the old blocking server, clients 2 and 3 were never
# even accepted, so nothing they sent could ever be seen.
for idx in (2, 0, 1):
    conns[idx].sendall(f"hello from client {idx+1}".encode())
    time.sleep(0.15)

conns[1].close()          # one client leaves; the others must survive
time.sleep(0.2)
conns[0].sendall(b"still alive after a peer disconnected")
time.sleep(0.3)

srv.terminate()
out = srv.stdout.read()
print("---- server log ----")
print(out)

connected = out.count("client connected")
received  = out.count("received")
ok = connected == 3 and received >= 4 and "client dropped" in out
print("---- result ----")
print(f"clients accepted: {connected}/3   recv events: {received}")
print("PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
