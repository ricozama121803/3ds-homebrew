#!/usr/bin/env bash
# Installs a project's .cia on the 3DS over Wi-Fi with FBI's Remote Install, so the SD card can stay in the 3DS.
#   On the 3DS: FBI -> Remote Install -> Receive URLs over the network   (it shows the 3DS's IP)
#   Here:       ./install-cia.sh beatpad [3ds-ip]       (the IP is remembered in tools/3ds-ip)
set -euo pipefail
root="$(cd "$(dirname "$0")" && pwd)"
proj="${1:?usage: ./install-cia.sh <project> [3ds-ip]}"
cia="$(ls "$root/$proj"/*.cia 2>/dev/null | head -1)"
[ -n "$cia" ] || { echo "no .cia in $proj/ (build it first: ./dkp-make.sh $proj)"; exit 1; }
ipfile="$root/tools/3ds-ip"
ip="${2:-$(cat "$ipfile" 2>/dev/null || true)}"
[ -n "$ip" ] || { read -rp "3DS IP address (shown in FBI's Remote Install screen): " ip; }
mkdir -p "$root/tools" && echo "$ip" > "$ipfile"
exec python3 - "$cia" "$ip" <<'PY'
import http.server, os, socket, socketserver, struct, sys, threading, urllib.parse
cia, ip = sys.argv[1], sys.argv[2]
d, name = os.path.dirname(cia), os.path.basename(cia)
with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:        # this PC's address on the 3DS's network
    s.connect((ip, 5000)); me = s.getsockname()[0]
class H(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **k): super().__init__(*a, directory=d, **k)
    def log_message(self, *a): pass
class S(socketserver.ThreadingMixIn, http.server.HTTPServer): daemon_threads = True
httpd = S((me, 8080), H)
threading.Thread(target=httpd.serve_forever, daemon=True).start()
url = f"http://{me}:8080/{urllib.parse.quote(name)}"
print(f"{name} ({os.path.getsize(cia) / 1e6:.1f} MB) -> 3DS at {ip}")
try:
    sock = socket.create_connection((ip, 5000), timeout=10)
except OSError as e:
    sys.exit(f"Couldn't reach the 3DS at {ip}:5000 ({e}).\nOpen FBI -> Remote Install -> Receive URLs over the network, and check the IP.")
payload = url.encode()
sock.sendall(struct.pack("!L", len(payload)) + payload)
print("Accept the prompt on the 3DS; FBI downloads and installs it (this waits until FBI is done)...")
sock.settimeout(None)
sock.recv(1)                               # FBI answers (or hangs up) once it has finished
sock.close(); httpd.shutdown()
print("Installed.")
PY
