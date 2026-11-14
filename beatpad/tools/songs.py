#!/usr/bin/env python3
"""BeatPad song library on the PC, and the Wi-Fi server the 3DS's "Get Songs" screen talks to.

    python3 tools/songs.py add  song.mp3 [more.mp3 ...]   convert into library/ (vocals split, charts made)
    python3 tools/songs.py list
    python3 tools/songs.py remove <id>
    python3 tools/songs.py serve                          share library/ over Wi-Fi until Ctrl+C

Title and artist come from the file's tags (or the file name); override with --title / --artist for a single file.
Protocol: see source/net.c. Ports: TCP 47320 (HTTP), UDP 47321 (queries in), UDP 47322 (replies/beacons out).
"""
import argparse, http.server, json, os, re, shutil, socket, socketserver, subprocess, sys, threading, time, urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))
LIBRARY = os.environ.get("BEATPAD_LIBRARY") or os.path.normpath(os.path.join(HERE, "..", "library"))
HTTP_PORT, QUERY_PORT, REPLY_PORT = 47320, 47321, 47322
SONG_FILES = ["info.txt", "beats.txt", "easy.chart", "normal.chart", "hard.chart", "audio.bpa"]


def tags(path):
    try:
        out = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format_tags=title,artist", "-of", "json", path],
                             check=True, capture_output=True, text=True).stdout
        t = {k.lower(): v for k, v in json.loads(out).get("format", {}).get("tags", {}).items()}
    except Exception:
        t = {}
    title = t.get("title") or os.path.splitext(os.path.basename(path))[0]
    return title.strip(), t.get("artist", "").strip()


def slug(s):
    s = re.sub(r"[^a-z0-9]+", "-", s.lower()).strip("-")
    return (s or "song")[:40]


def clean(s):          # keep the 3DS-side parsing simple
    return re.sub(r"[\t\n\r=]", " ", s).strip()[:60]


def cmd_add(a):
    os.makedirs(LIBRARY, exist_ok=True)
    if len(a.files) > 1 and (a.title or a.artist):
        sys.exit("--title/--artist only work with a single file")
    for i, f in enumerate(a.files, 1):
        if not os.path.isfile(f):
            print(f"skipping {f}: not a file"); continue
        title, artist = tags(f)
        title, artist = clean(a.title or title), clean(a.artist if a.artist is not None else artist)
        sid = slug(title)
        out = os.path.join(LIBRARY, sid)
        print(f"[{i}/{len(a.files)}] {title}" + (f" - {artist}" if artist else "") + f"  ->  library/{sid}")
        t0 = time.time()
        subprocess.run([sys.executable, os.path.join(HERE, "make_song.py"), f, out, "--title", title, "--artist", artist,
                        "--separate"], check=True)
        print(f"    done in {time.time() - t0:.0f}s")


def songs():
    out = []
    if not os.path.isdir(LIBRARY): return out
    for sid in sorted(os.listdir(LIBRARY)):
        d = os.path.join(LIBRARY, sid)
        if not all(os.path.isfile(os.path.join(d, f)) for f in SONG_FILES): continue
        info = dict(l.rstrip("\n").split("=", 1) for l in open(os.path.join(d, "info.txt")) if "=" in l)
        sizes = [os.path.getsize(os.path.join(d, f)) for f in SONG_FILES]
        version = str(int(max(os.path.getmtime(os.path.join(d, f)) for f in SONG_FILES)))
        out.append(dict(id=sid, title=info.get("title", sid), artist=info.get("artist", ""), version=version,
                        files=list(zip(SONG_FILES, sizes)), bytes=sum(sizes), length=int(info.get("length_ms", 0))))
    return out


def cmd_list(a):
    s = songs()
    if not s: print("library is empty: add songs with  python3 tools/songs.py add song.mp3"); return
    for x in s:
        print(f"{x['id']:28s} {x['title']}" + (f" - {x['artist']}" if x["artist"] else "") +
              f"  ({x['length'] // 60000}:{x['length'] // 1000 % 60:02d}, {x['bytes'] / 1e6:.1f} MB)")


def cmd_remove(a):
    d = os.path.join(LIBRARY, a.id)
    if not os.path.isdir(d): sys.exit(f"no song '{a.id}' (see: songs.py list)")
    shutil.rmtree(d); print(f"removed {a.id}")


def lan_ips_and_broadcasts():
    """(ip, broadcast) for each IPv4 interface that isn't loopback or a container bridge."""
    res = []
    try:
        out = subprocess.run(["ip", "-4", "-o", "addr", "show"], capture_output=True, text=True).stdout
        for line in out.splitlines():
            parts = line.split()
            dev = parts[1]
            if dev == "lo" or dev.startswith(("docker", "br-", "veth", "virbr", "podman")): continue
            ip = parts[3].split("/")[0]
            brd = parts[parts.index("brd") + 1] if "brd" in parts else None
            res.append((ip, brd))
    except Exception:
        pass
    return res


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a): pass

    def do_GET(self):
        path = urllib.parse.unquote(self.path)
        if path == "/index.txt":
            lines = []
            for s in songs():
                lines.append("\t".join(["song", s["id"], s["version"], s["title"], s["artist"], str(s["bytes"])]))
                lines += ["\t".join(["file", s["id"], f, str(n)]) for f, n in s["files"]]
            body = ("\n".join(lines) + "\n").encode()
            if not self.server.seen.get(self.client_address[0]):
                self.server.seen[self.client_address[0]] = True
                print(f"3DS connected from {self.client_address[0]}")
            self.send_response(200); self.send_header("Content-Length", str(len(body))); self.end_headers()
            self.wfile.write(body); return
        m = re.fullmatch(r"/songs/([a-z0-9-]+)/([a-z.]+)", path)
        if m and m.group(2) in SONG_FILES:
            f = os.path.join(LIBRARY, m.group(1), m.group(2))
            if os.path.isfile(f):
                size = os.path.getsize(f)
                self.send_response(200); self.send_header("Content-Length", str(size)); self.end_headers()
                with open(f, "rb") as fh:
                    shutil.copyfileobj(fh, self.wfile, 65536)
                if m.group(2) == "audio.bpa": print(f"sent {m.group(1)}")
                return
        self.send_error(404)


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def cmd_serve(a):
    sys.stdout.reconfigure(line_buffering=True)
    name = clean(socket.gethostname().split(".")[0]).replace(" ", "-") or "PC"
    httpd = Server(("0.0.0.0", HTTP_PORT), Handler)
    httpd.seen = {}
    reply = f"BEATPAD! {HTTP_PORT} {name}".encode()

    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    udp.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    udp.bind(("0.0.0.0", QUERY_PORT))

    def answer():
        while True:
            data, addr = udp.recvfrom(64)
            if data.startswith(b"BEATPAD?"):
                udp.sendto(reply, (addr[0], REPLY_PORT))

    def beacon():
        while True:
            targets = {"255.255.255.255"} | {b for _, b in lan_ips_and_broadcasts() if b}
            for t in targets:
                try: udp.sendto(reply, (t, REPLY_PORT))
                except OSError: pass
            time.sleep(1.0)

    threading.Thread(target=answer, daemon=True).start()
    threading.Thread(target=beacon, daemon=True).start()
    lib = songs()
    ips = ", ".join(ip for ip, _ in lan_ips_and_broadcasts()) or "?"
    print(f"Sharing {len(lib)} song(s) from {LIBRARY}")
    print(f"On the 3DS: BeatPad -> Y (Get Songs). It finds this PC by itself; if not, enter {ips}")
    print("Ctrl+C to stop.")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("add"); p.add_argument("files", nargs="+"); p.add_argument("--title"); p.add_argument("--artist")
    p.set_defaults(fn=cmd_add)
    sub.add_parser("list").set_defaults(fn=cmd_list)
    p = sub.add_parser("remove"); p.add_argument("id"); p.set_defaults(fn=cmd_remove)
    sub.add_parser("serve").set_defaults(fn=cmd_serve)
    a = ap.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
