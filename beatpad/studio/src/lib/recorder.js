// Records the sound of another browser tab (Chrome/Edge "Also share tab audio") as WebM/Opus.
// The server trims the silence at both ends, so recording can start before you press play.

export const MAX_RECORD_SECONDS = 15 * 60;
const HEARD_DB = -45;        // louder than this: the song has started
const SILENT_DB = -55;       // quieter than this: silence

export const canRecordTabs = () =>
  typeof window !== "undefined" && !!navigator.mediaDevices?.getDisplayMedia && "MediaRecorder" in window;

function pickMime() {
  for (const t of ["audio/webm;codecs=opus", "audio/webm", "audio/ogg;codecs=opus"]) if (MediaRecorder.isTypeSupported(t)) return t;
  return "";
}

/**
 * Opens the browser's tab picker and records. Throws a friendly Error if nothing usable was shared.
 * cb: onLevel(dB), onTick(seconds), onHeard(), onQuiet(seconds of silence after the song started)
 * autoStop: () => boolean, checked live; stops after 5 s of silence once sound has been heard.
 * Returns { stop(), cancel(), done: Promise<{ blob, seconds } | null> }.
 */
export async function startTabRecording({ onLevel, onTick, onHeard, onQuiet, autoStop }) {
  let stream;
  try {
    stream = await navigator.mediaDevices.getDisplayMedia({
      video: { displaySurface: "browser" },       // Chrome needs video to show the tab picker
      audio: { echoCancellation: false, noiseSuppression: false, autoGainControl: false, suppressLocalAudioPlayback: false },
      preferCurrentTab: false, selfBrowserSurface: "exclude", systemAudio: "include", surfaceSwitching: "exclude",
    });
  } catch (err) {
    if (err?.name === "NotAllowedError") throw new Error("Recording was cancelled.");
    throw new Error(`Couldn't start recording: ${err?.message || err}`);
  }
  const audio = stream.getAudioTracks();
  if (!audio.length) {
    stream.getTracks().forEach((t) => t.stop());
    throw new Error("That share had no sound. Pick a browser tab (not a window or screen) and keep “Also share tab audio” switched on.");
  }
  // Only the sound is recorded; the video track stays alive so the browser doesn't end the share early.
  const audioStream = new MediaStream(audio);
  const ctx = new AudioContext();
  const analyser = ctx.createAnalyser();
  analyser.fftSize = 2048;
  ctx.createMediaStreamSource(audioStream).connect(analyser);
  const buf = new Float32Array(analyser.fftSize);

  const recorder = new MediaRecorder(audioStream, { mimeType: pickMime(), audioBitsPerSecond: 256000 });
  const chunks = [];
  recorder.ondataavailable = (e) => e.data.size && chunks.push(e.data);

  const t0 = performance.now();
  let raf = 0, heard = false, silentSince = null, cancelled = false, finished = false;
  let resolveDone;
  const done = new Promise((r) => (resolveDone = r));

  const finish = () => {
    if (finished) return;
    finished = true;
    cancelAnimationFrame(raf);
    stream.getTracks().forEach((t) => t.stop());
    ctx.close();
  };
  recorder.onstop = () => {
    finish();
    if (cancelled || !heard) return resolveDone(cancelled ? null : { blob: null, seconds: 0 });
    resolveDone({ blob: new Blob(chunks, { type: recorder.mimeType || "audio/webm" }), seconds: (performance.now() - t0) / 1000 });
  };
  const stop = () => recorder.state === "recording" && recorder.stop();
  audio[0].onended = stop;                                   // "Stop sharing" in the browser's bar

  const loop = () => {
    analyser.getFloatTimeDomainData(buf);
    let sum = 0;
    for (const v of buf) sum += v * v;
    const db = 10 * Math.log10(sum / buf.length + 1e-12);
    const secs = (performance.now() - t0) / 1000;
    onLevel?.(db);
    onTick?.(secs);
    if (db > HEARD_DB) {
      if (!heard) onHeard?.();
      heard = true; silentSince = null;
      onQuiet?.(0);
    } else if (db < SILENT_DB && heard) {
      silentSince ??= performance.now();
      const quiet = (performance.now() - silentSince) / 1000;
      onQuiet?.(quiet);
      if (autoStop?.() && quiet > 5) return stop();
    }
    if (secs > MAX_RECORD_SECONDS) return stop();
    raf = requestAnimationFrame(loop);
  };
  recorder.start(1000);
  loop();
  return { stop, cancel: () => { cancelled = true; recorder.state === "recording" ? recorder.stop() : (finish(), resolveDone(null)); }, done };
}
