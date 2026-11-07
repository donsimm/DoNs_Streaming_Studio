# Übernimmt über die Kern-Schnittstelle eine grosse Datei (add_image_source) und misst die Zeit.
import json, socket, sys, time
sock_path, image = sys.argv[1], sys.argv[2]
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(sock_path)
f = s.makefile("rwb")
def send(o):
    f.write((json.dumps(o) + "\n").encode()); f.flush()
send({"program": "dss-app", "protocol_version": 4, "type": "hello", "version": "0.1.0"})
overlay = None
while overlay is None:
    m = json.loads(f.readline())
    if m["type"] == "overlay_list":
        e = m["entries"][0]; overlay = e.get("id") or e["overlay"]["id"]
t0 = time.monotonic()
send({"type": "add_image_source", "request_id": 1, "overlay_id": overlay, "file": image})
while True:
    m = json.loads(f.readline())
    if m["type"] == "change_result" and m["request_id"] == 1:
        print(f"Antwort nach {time.monotonic() - t0:.1f} s: ok={m['ok']} {m.get('message')}")
        break
