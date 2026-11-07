# Übernimmt über die Kern-Schnittstelle (Version 5) eine grosse Datei (add_image_source) und
# fragt währenddessen alle 0.5 s get_overlay_list an; misst Gesamtzeit und Antwortzeiten.
# Aufruf: watchdog-gross.py SOCKET DATEI
import json, select, socket, sys, time
sock_path, image = sys.argv[1], sys.argv[2]
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(sock_path)
buf = b""
def send(o):
    s.sendall((json.dumps(o) + "\n").encode())
def lines(timeout):
    global buf
    r, _, _ = select.select([s], [], [], timeout)
    if r:
        data = s.recv(1 << 20)
        if not data:
            raise SystemExit("Verbindung getrennt")
        buf += data
    out = []
    while b"\n" in buf:
        line, buf = buf.split(b"\n", 1)
        out.append(json.loads(line))
    return out
send({"program": "watchdog-gross", "protocol_version": 5, "type": "hello", "version": "0.1.0"})
overlay = None
while overlay is None:
    for m in lines(1):
        if m["type"] == "overlay_list":
            e = m["entries"][0]; overlay = e.get("id") or e["overlay"]["id"]
t0 = time.monotonic()
send({"type": "add_image_source", "request_id": 1, "overlay_id": overlay, "file": image})
replies, asked, done = [], None, None
while done is None:
    now = time.monotonic()
    if asked is None:
        send({"type": "get_overlay_list"}); asked = now
    for m in lines(0.05):
        if m["type"] == "overlay_list" and asked is not None:
            replies.append((time.monotonic() - asked) * 1000); asked = None
            time.sleep(0.5)
        elif m["type"] == "change_result" and m["request_id"] == 1:
            done = m
    if asked is not None and time.monotonic() - asked > 5:
        raise SystemExit("keine Antwort auf get_overlay_list nach 5 s")
replies.sort()
print(f"Antwort nach {time.monotonic() - t0:.1f} s: ok={done['ok']} {done.get('message') or ''}")
print(f"get_overlay_list während des Übernehmens: {len(replies)} Antworten, min {replies[0]:.1f} / "
      f"Median {replies[len(replies)//2]:.1f} / max {replies[-1]:.1f} ms")
