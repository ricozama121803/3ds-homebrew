// Talks to the song server (tools/songs.py), which serves this page.

export async function getState() {
  const r = await fetch("/api/state", { cache: "no-store" });
  if (!r.ok) throw new Error(`HTTP ${r.status}`);
  return r.json();
}

// Uploads with progress (fetch can't report upload progress). Resolves to the job id.
export function uploadSong(blob, { name, title, artist, trim }, onProgress) {
  const q = new URLSearchParams({ name, title, artist, trim: trim ? "1" : "0" });
  return new Promise((resolve, reject) => {
    const x = new XMLHttpRequest();
    x.open("POST", "/api/upload?" + q);
    x.setRequestHeader("Content-Type", blob.type || "application/octet-stream");
    x.upload.onprogress = (e) => e.lengthComputable && onProgress?.(e.loaded / e.total);
    x.onload = () => {
      let data = {};
      try { data = JSON.parse(x.responseText); } catch {}
      x.status === 200 && data.job ? resolve(data.job) : reject(new Error(data.error || `Upload failed (HTTP ${x.status})`));
    };
    x.onerror = () => reject(new Error("Upload failed: is ./serve-songs.sh still running?"));
    x.send(blob);
  });
}

export async function deleteSong(id) {
  const r = await fetch("/api/delete?id=" + encodeURIComponent(id), { method: "POST" });
  if (!r.ok) throw new Error(`HTTP ${r.status}`);
}

export const fmtTime = (ms) => `${Math.floor(ms / 60000)}:${String(Math.floor(ms / 1000) % 60).padStart(2, "0")}`;
export const fmtSecs = (s) => fmtTime(s * 1000);
