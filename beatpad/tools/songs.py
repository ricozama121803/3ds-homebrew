#!/usr/bin/env python3
"""BeatPad song library on the PC, the Wi-Fi server the 3DS's "Get Songs" screen talks to, and the Song Studio
web page (capture audio from a browser tab, or drop files) at http://localhost:47330.

    python3 tools/songs.py add  song.mp3 [more.mp3 ...]   convert into library/ (vocals split, charts made)
    python3 tools/songs.py list
    python3 tools/songs.py remove <id>
    python3 tools/songs.py serve                          share library/ over Wi-Fi + Song Studio, until Ctrl+C

Title and artist come from the file's tags (or the file name); override with --title / --artist for a single file.
Each song keeps a copy of its source audio (library/<id>/source.*) so it can always be regenerated.
Protocol: see source/net.c. Ports: TCP 47320 (3DS HTTP), UDP 47321 (queries in), UDP 47322 (replies/beacons out),
TCP 47330 (Song Studio, this PC only).
"""
import argparse, http.server, json, os, re, shutil, socket, socketserver, subprocess, sys, threading, time, urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))
LIBRARY = os.environ.get("BEATPAD_LIBRARY") or os.path.normpath(os.path.join(HERE, "..", "library"))
STEMS = os.path.normpath(os.path.join(HERE, "..", ".stems", "htdemucs"))
INCOMING = os.path.join(LIBRARY, ".incoming")
MAKE_SONG = os.path.join(HERE, "make_song.py")
STUDIO = os.path.normpath(os.path.join(HERE, "..", "studio", "out"))     # built by `npm run build` in studio/
MIME = {".html": "text/html; charset=utf-8", ".js": "text/javascript", ".css": "text/css", ".svg": "image/svg+xml",
        ".png": "image/png", ".ico": "image/x-icon", ".woff2": "font/woff2", ".json": "application/json", ".txt": "text/plain"}
HTTP_PORT, QUERY_PORT, REPLY_PORT, WEB_PORT = 47320, 47321, 47322, 47330
SONG_FILES = ["info.txt", "beats.txt", "easy.chart", "normal.chart", "hard.chart", "audio.bpa"]
MAX_UPLOAD = 300 * 1024 * 1024
# drop the silence before the music starts and after it ends (tab recordings begin before you press play)
TRIM = ("silenceremove=start_periods=1:start_silence=0.05:start_threshold=-55dB,areverse,"
        "silenceremove=start_periods=1:start_silence=0.3:start_threshold=-55dB,areverse")


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


def convert(src, title, artist, trim=False, progress=None, keep_stems=True):
    """Turns an audio file into library/<id>. The song is built in library/.work-<id> and swapped in when complete,
    so a 3DS never downloads half of one. progress(stage, percent) gets "preparing", "separating", "charting".
    Returns the song id; raises RuntimeError with the tail of the converter's output on failure."""
    progress = progress or (lambda stage, pct=None: None)
    sid = slug(title)
    os.makedirs(INCOMING, exist_ok=True)
    work = os.path.join(LIBRARY, f".work-{sid}")
    shutil.rmtree(work, ignore_errors=True)
    inp, tmp = src, None
    if trim:
        progress("preparing")
        tmp = os.path.join(INCOMING, f"{sid}-{int(time.time() * 1000)}.wav")      # unique: stems are cached by name
        r = subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", src, "-af", TRIM, "-ar", "44100", "-ac", "2", tmp],
                           capture_output=True, text=True)
        if r.returncode: raise RuntimeError("couldn't read the audio: " + r.stderr.strip()[-300:])
        inp = tmp
    cmd = [sys.executable, "-u", MAKE_SONG, inp, work, "--title", title, "--artist", artist, "--separate"]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    tail, buf = [], b""
    while True:
        chunk = proc.stdout.read1(4096)
        if not chunk: break
        buf += chunk
        parts = re.split(rb"[\r\n]", buf)
        buf = parts.pop()
        for p in parts:
            if not p.strip(): continue
            tail = (tail + [p.decode(errors="replace")])[-12:]
            m = re.search(rb"(\d+)%\|", p)
            if m: progress("separating", int(m.group(1)))
            elif p.startswith(b"separating"): progress("separating", 0)
            elif p.startswith((b"tempo", b"grid fit", b"stems are")): progress("charting")
    if proc.wait():
        shutil.rmtree(work, ignore_errors=True)
        if tmp: os.remove(tmp)
        raise RuntimeError("\n".join(l for l in tail if "%|" not in l)[-600:] or "conversion failed")
    ext = os.path.splitext(src)[1].lower() or ".audio"
    shutil.copy(src, os.path.join(work, "source" + ext))
    dest = os.path.join(LIBRARY, sid)
    if os.path.isdir(dest):                        # replace an older version
        old = os.path.join(LIBRARY, f".old-{sid}")
        shutil.rmtree(old, ignore_errors=True)
        os.rename(dest, old)
        os.rename(work, dest)
        shutil.rmtree(old, ignore_errors=True)
    else:
        os.rename(work, dest)
    if tmp:
        os.remove(tmp)
        if not keep_stems: shutil.rmtree(os.path.join(STEMS, os.path.splitext(os.path.basename(tmp))[0]), ignore_errors=True)
    return sid


def cmd_add(a):
    os.makedirs(LIBRARY, exist_ok=True)
    if len(a.files) > 1 and (a.title or a.artist):
        sys.exit("--title/--artist only work with a single file")
    for i, f in enumerate(a.files, 1):
        if not os.path.isfile(f):
            print(f"skipping {f}: not a file"); continue
        title, artist = tags(f)
        title, artist = clean(a.title or title), clean(a.artist if a.artist is not None else artist)
        print(f"[{i}/{len(a.files)}] {title}" + (f" - {artist}" if artist else "") + f"  ->  library/{slug(title)}")
        t0, last = time.time(), [None]

        def show(stage, pct=None):
            if stage == "separating":
                print(f"\r    separating vocals {pct or 0:3d}%", end="", flush=True)
            elif stage != last[0]:
                print(("\n" if last[0] == "separating" else "") + f"    {stage}...", flush=True)
            last[0] = stage
        try:
            convert(f, title, artist, progress=show)
        except RuntimeError as e:
            print(f"\n    FAILED:\n{e}"); continue
        print(f"    done in {time.time() - t0:.0f}s")


def songs():
    out = []
    if not os.path.isdir(LIBRARY): return out
    for sid in sorted(os.listdir(LIBRARY)):
        d = os.path.join(LIBRARY, sid)
        if sid.startswith(".") or not all(os.path.isfile(os.path.join(d, f)) for f in SONG_FILES): continue
        info = dict(l.rstrip("\n").split("=", 1) for l in open(os.path.join(d, "info.txt")) if "=" in l)
        sizes = [os.path.getsize(os.path.join(d, f)) for f in SONG_FILES]
        version = str(int(max(os.path.getmtime(os.path.join(d, f)) for f in SONG_FILES)))
        out.append(dict(id=sid, title=info.get("title", sid), artist=info.get("artist", ""), version=version,
                        files=list(zip(SONG_FILES, sizes)), bytes=sum(sizes), length=int(info.get("length_ms", 0)),
                        bpm=float(info.get("bpm", 0)), notes=int(info.get("notes_normal", 0))))
    return out


def cmd_list(a):
    s = songs()
    if not s: print("library is empty: add songs with  python3 tools/songs.py add song.mp3"); return
    for x in s:
        print(f"{x['id']:28s} {x['title']}" + (f" - {x['artist']}" if x["artist"] else "") +
              f"  ({x['length'] // 60000}:{x['length'] // 1000 % 60:02d}, {x['bytes'] / 1e6:.1f} MB)")


def cmd_remove(a):
    d = os.path.join(LIBRARY, a.id)
    if not os.path.isdir(d) or a.id.startswith("."): sys.exit(f"no song '{a.id}' (see: songs.py list)")
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


# ---- the 3DS side ------------------------------------------------------------------------
SEEN = {}            # 3DS ip -> last time it asked for the song list
SENT = {}            # song id -> {"ip", "t"}: when a 3DS last finished downloading it


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
            ip = self.client_address[0]
            if not ip.startswith("127."):                  # (this PC checking the list isn't a 3DS)
                if ip not in SEEN: print(f"3DS connected from {ip}")
                SEEN[ip] = time.time()
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
                if m.group(2) == "audio.bpa":            # the 3DS fetches the audio last
                    print(f"sent {m.group(1)} to {self.client_address[0]}")
                    SENT[m.group(1)] = dict(ip=self.client_address[0], t=time.time())
                return
        self.send_error(404)


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


# ---- Song Studio: conversion queue + local web page --------------------------------------------
class Jobs:
    def __init__(self):
        self.lock = threading.Lock()
        self.items, self.next_id = [], 1
        self.wake = threading.Event()
        threading.Thread(target=self.run, daemon=True).start()

    def add(self, path, title, artist, trim):
        with self.lock:
            j = dict(id=self.next_id, file=path, title=title, artist=artist, trim=trim, stage="queued", pct=None,
                     error=None, sid=None, t=time.time())
            self.next_id += 1
            self.items.append(j)
        self.wake.set()
        return j["id"]

    def snapshot(self):
        with self.lock:
            return [{k: v for k, v in j.items() if k != "file"} for j in self.items[-20:]]

    def run(self):
        while True:
            self.wake.wait(); self.wake.clear()
            while True:
                with self.lock:
                    j = next((x for x in self.items if x["stage"] == "queued"), None)
                if not j: break
                if not j["title"]:
                    t, ar = tags(j["file"])
                    j["title"], j["artist"] = clean(t), clean(j["artist"] or ar)
                print(f"converting {j['title']}...")

                def prog(stage, pct=None, j=j):
                    j["stage"], j["pct"] = stage, pct
                try:
                    j["sid"] = convert(j["file"], j["title"], j["artist"], trim=j["trim"], progress=prog, keep_stems=False)
                    j["done_t"] = time.time()
                    j["stage"] = "done"
                    print(f"ready: {j['title']} (library/{j['sid']})")
                except Exception as e:
                    j["stage"], j["error"] = "failed", str(e)
                    print(f"failed: {j['title']}: {e}")
                finally:
                    try: os.remove(j["file"])
                    except OSError: pass


def make_web_handler(jobs):
    class Web(http.server.BaseHTTPRequestHandler):
        def log_message(self, *a): pass

        def local_only(self):
            # only this PC, and only pages it serves (stops other websites from poking the API)
            host = (self.headers.get("Host") or "").split(":")[0]
            origin = self.headers.get("Origin")
            ok = host in ("localhost", "127.0.0.1") and (origin is None or re.fullmatch(r"http://(localhost|127\.0\.0\.1):(%d|4310)" % WEB_PORT, origin))
            if not ok: self.send_error(403)
            return ok

        def send_json(self, obj, code=200):
            body = json.dumps(obj).encode()
            self.send_response(code); self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body))); self.end_headers(); self.wfile.write(body)

        def do_GET(self):
            if not self.local_only(): return
            path = urllib.parse.unquote(urllib.parse.urlparse(self.path).path)
            if path == "/api/state":
                now = time.time()
                ds = max(SEEN.items(), key=lambda kv: kv[1]) if SEEN else None
                self.send_json(dict(jobs=jobs.snapshot(), sent=SENT,
                                    songs=[{k: s[k] for k in ("id", "title", "artist", "length", "bytes", "bpm", "notes")} for s in songs()],
                                    ds=dict(ip=ds[0], ago=round(now - ds[1])) if ds else None,
                                    lan=[ip for ip, _ in lan_ips_and_broadcasts()]))
            else:
                self.send_static(path)

        def send_static(self, path):
            if not os.path.isdir(STUDIO):
                body = (b"<!doctype html><meta charset=utf-8><title>Song Studio</title><body style='font:16px system-ui;background:#111;color:#ddd;padding:40px'>"
                        b"<h1>Song Studio isn't built yet</h1><p>In <code>beatpad/studio</code> run <code>npm install && npm run build</code> "
                        b"(./serve-songs.sh does this for you when npm is installed), then reload.</p>")
                self.send_response(200); self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(body))); self.end_headers(); self.wfile.write(body); return
            rel = path.lstrip("/") or "index.html"
            f = os.path.normpath(os.path.join(STUDIO, rel))
            if not f.startswith(STUDIO + os.sep): return self.send_error(404)
            if os.path.isdir(f): f = os.path.join(f, "index.html")
            if not os.path.isfile(f) and os.path.isfile(f + ".html"): f += ".html"
            if not os.path.isfile(f): return self.send_error(404)
            body = open(f, "rb").read()
            self.send_response(200)
            self.send_header("Content-Type", MIME.get(os.path.splitext(f)[1], "application/octet-stream"))
            self.send_header("Content-Length", str(len(body))); self.end_headers(); self.wfile.write(body)

        def do_POST(self):
            if not self.local_only(): return
            u = urllib.parse.urlparse(self.path)
            q = {k: v[0] for k, v in urllib.parse.parse_qs(u.query).items()}
            if u.path == "/api/upload":
                n = int(self.headers.get("Content-Length") or 0)
                if n <= 0 or n > MAX_UPLOAD: return self.send_json(dict(error="file too big or empty"), 413)
                name = re.sub(r"[^A-Za-z0-9._-]+", "_", q.get("name", "audio"))[-60:]
                os.makedirs(INCOMING, exist_ok=True)
                path = os.path.join(INCOMING, f"upload-{int(time.time() * 1000)}-{name}")
                with open(path, "wb") as f:
                    left = n
                    while left > 0:
                        chunk = self.rfile.read(min(65536, left))
                        if not chunk: break
                        f.write(chunk); left -= len(chunk)
                jid = jobs.add(path, clean(q.get("title", "")), clean(q.get("artist", "")), q.get("trim") == "1")
                return self.send_json(dict(job=jid))
            if u.path == "/api/delete":
                sid = q.get("id", "")
                d = os.path.join(LIBRARY, sid)
                if re.fullmatch(r"[a-z0-9-]+", sid) and os.path.isdir(d):
                    shutil.rmtree(d); print(f"removed {sid}")
                    return self.send_json(dict(ok=True))
                return self.send_json(dict(error="no such song"), 404)
            self.send_error(404)
    return Web


def cmd_serve(a):
    sys.stdout.reconfigure(line_buffering=True)
    name = clean(socket.gethostname().split(".")[0]).replace(" ", "-") or "PC"
    httpd = Server(("0.0.0.0", HTTP_PORT), Handler)
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
    web = Server(("127.0.0.1", WEB_PORT), make_web_handler(Jobs()))
    threading.Thread(target=web.serve_forever, daemon=True).start()
    lib = songs()
    ips = ", ".join(ip for ip, _ in lan_ips_and_broadcasts()) or "?"
    print(f"Sharing {len(lib)} song(s) from {LIBRARY}")
    print(f"Song Studio (record a browser tab, drop files): http://localhost:{WEB_PORT}")
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
