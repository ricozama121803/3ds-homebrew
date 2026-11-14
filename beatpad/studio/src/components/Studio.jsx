"use client";
import { useEffect, useRef, useState } from "react";
import {
  IconBrandChrome, IconUpload, IconPlayerStopFilled, IconX, IconCheck, IconWifi, IconDeviceGamepad2, IconMusic,
  IconTrash, IconRefresh, IconAlertTriangle, IconPlus, IconLoader2, IconSparkles,
} from "@tabler/icons-react";
import { BackgroundBeams } from "@/components/ui/background-beams";
import { Spotlight } from "@/components/ui/spotlight-new";
import { FileUpload } from "@/components/ui/file-upload";
import { HoverBorderGradient } from "@/components/ui/hover-border-gradient";
import StepCard, { Hint } from "@/components/StepCard";
import StepNav, { useActiveStep } from "@/components/StepNav";
import Footer from "@/components/Footer";
import Mascot from "@/components/Mascot";
import { Spinner, Status } from "@/components/Spinner";
import { canRecordTabs, startTabRecording } from "@/lib/recorder";
import { getState, uploadSong, deleteSong, fmtTime, fmtSecs } from "@/lib/api";
import { cn } from "@/lib/utils";

const STEPS = [["connect", "Connect your 3DS"], ["song", "Pick a song"], ["name", "Name it"], ["convert", "Make the charts"], ["play", "Play it"]];
const STEP_IDS = STEPS.map(([id]) => `step-${id}`);
const LANES = ["#3fd96a", "#f04848", "#f5d03a", "#3d8cf5", "#ff9a2e", "#c160f0"];
const AUDIO_ACCEPT = { "audio/*": [".mp3", ".m4a", ".aac", ".flac", ".wav", ".ogg", ".opus", ".webm"] };

const titleFromFile = (name) => name.replace(/\.[^.]+$/, "").replace(/[_-]+/g, " ").replace(/\s+/g, " ").trim();

export default function Studio() {
  const [server, setServer] = useState(null);         // /api/state
  const [serverDown, setServerDown] = useState(false);
  const [mode, setMode] = useState("record");         // "record" | "file"
  const [source, setSource] = useState(null);         // { blob, name, url, seconds?, recorded }
  const [title, setTitle] = useState("");
  const [artist, setArtist] = useState("");
  const [upload, setUpload] = useState(null);         // { pct } while uploading
  const [jobId, setJobId] = useState(null);
  const [error, setError] = useState("");
  const [deleting, setDeleting] = useState(null);

  // poll the song server
  useEffect(() => {
    let alive = true;
    const tick = async () => {
      try { const s = await getState(); if (alive) { setServer(s); setServerDown(false); } }
      catch { if (alive) setServerDown(true); }
    };
    tick();
    const t = setInterval(tick, 1000);
    return () => { alive = false; clearInterval(t); };
  }, []);

  const job = server?.jobs?.find((j) => j.id === jobId) || null;
  const ds = server?.ds;
  const dsOnline = !!ds && ds.ago < 15;
  const sentAt = job?.sid && server?.sent?.[job.sid];
  const delivered = !!(job?.stage === "done" && sentAt && sentAt.t >= job.done_t);

  const [activeStep, setActiveStep] = useActiveStep(STEP_IDS);
  const stepDone = {
    connect: dsOnline || !!ds, song: !!source, name: !!title.trim(), convert: job?.stage === "done", play: delivered,
  };
  const navSteps = STEPS.map(([id, label], i) => ({ id: STEP_IDS[i], n: i + 1, label, done: stepDone[id] }));
  const goto = (id) => document.getElementById(`step-${id}`)?.scrollIntoView({ behavior: "smooth", block: "start" });

  const pickSource = (s) => {
    setSource((old) => { if (old?.url) URL.revokeObjectURL(old.url); return s; });
    setJobId(null); setError("");
    if (s && !s.recorded && !title) setTitle(titleFromFile(s.name));
    if (s) setTimeout(() => goto("name"), 250);
  };

  const convert = async () => {
    if (!source || !title.trim()) return;
    setError(""); setUpload({ pct: 0 });
    try {
      const id = await uploadSong(source.blob, { name: source.name, title: title.trim(), artist: artist.trim(), trim: source.recorded },
                                  (pct) => setUpload({ pct }));
      setJobId(id);
    } catch (e) {
      setError(e.message);
    } finally {
      setUpload(null);
    }
  };

  const another = () => {
    pickSource(null); setTitle(""); setArtist(""); setJobId(null);
    setTimeout(() => goto("song"), 50);
  };

  const remove = async (s) => {
    if (!confirm(`Delete “${s.title}” from your PC library? Copies already on the 3DS stay until you delete them there.`)) return;
    setDeleting(s.id);
    try { await deleteSong(s.id); } finally { setDeleting(null); }
  };

  const busy = !!upload || (job && !["done", "failed"].includes(job.stage));

  return (
    <div className="relative min-h-screen overflow-x-clip">
      <header className="relative overflow-hidden border-b border-neutral-900">
        <Spotlight />
        <BackgroundBeams />
        <div className="relative z-10 mx-auto flex max-w-7xl items-center justify-between gap-6 px-4 pb-14 pt-16 sm:px-6 sm:pt-20">
          <div>
            <p className="mb-3 flex gap-1 text-sm font-extrabold tracking-[0.35em]" aria-hidden>
              {"BEATPAD".split("").map((c, i) => <span key={i} style={{ color: LANES[i % 6] }}>{c}</span>)}
            </p>
            <h1 className="bg-gradient-to-b from-white to-neutral-400 bg-clip-text text-4xl font-bold tracking-tight text-transparent sm:text-6xl">
              Song Studio
            </h1>
            <p className="mt-4 max-w-2xl text-base text-neutral-400 sm:text-lg">
              Record a song playing in another tab or drop in a file. It finds the beat, follows the singing, writes Easy,
              Normal and Hard charts, and sends the song to BeatPad on your 3DS over Wi-Fi.
            </p>
          </div>
          <Mascot className="h-24 w-24 shrink-0 sm:h-36 sm:w-36 lg:mr-16 lg:h-40 lg:w-40" />
        </div>
      </header>

      <main className="relative z-10 mx-auto grid max-w-7xl grid-cols-[minmax(0,1fr)] items-start gap-8 px-4 py-10 sm:px-6 lg:grid-cols-[minmax(0,1fr)_360px] xl:grid-cols-[170px_minmax(0,1fr)_360px]">
        <aside className="hidden self-stretch xl:block" aria-label="Step navigation">
          <StepNav steps={navSteps} active={activeStep} setActive={setActiveStep} variant="rail" />
        </aside>

        <div className="flex flex-col gap-6">
          <StepNav steps={navSteps} active={activeStep} setActive={setActiveStep} variant="bar" />
          {serverDown && (
            <div className="flex items-start gap-3 rounded-xl border border-red-500/40 bg-red-500/10 p-4 text-sm text-red-200">
              <IconAlertTriangle className="mt-0.5 shrink-0" size={18} />
              <div>The song server isn’t answering. In the <code className="text-red-100">beatpad</code> folder run <code className="text-red-100">./serve-songs.sh</code>, then this page reconnects by itself.</div>
            </div>
          )}

          {/* 1 ------------------------------------------------------------------------------------------------- */}
          <StepCard id="step-connect" n={1} title="Connect your 3DS" aside={dsOnline ? "Connected" : ds ? "Seen earlier" : "Optional"}>
            {dsOnline ? (
              <Status kind="ok" icon={<IconWifi size={18} />}>Your 3DS is connected ({ds.ip}). New songs show up in its Get Songs list by themselves.</Status>
            ) : (
              <>
                <Status kind="wait">{ds ? `Your 3DS was last here ${ds.ago < 120 ? `${ds.ago} s` : `${Math.round(ds.ago / 60)} min`} ago. Waiting for it…` : "Waiting for your 3DS…"}</Status>
                <ol className="mt-4 grid gap-3 sm:grid-cols-3">
                  {[
                    ["Same Wi-Fi", "Your 3DS and this PC need to be on the same network."],
                    ["Open BeatPad", "Start BeatPad from the HOME Menu."],
                    ["Press Y", "Y opens Get Songs, which finds this PC by itself."],
                  ].map(([h, t], i) => (
                    <li key={h} className="rounded-xl border border-neutral-800 bg-neutral-950/60 p-4">
                      <p className="text-xs font-semibold uppercase tracking-wider text-neutral-500">Step {i + 1}</p>
                      <p className="mt-1 font-medium text-neutral-100">{h}</p>
                      <p className="mt-1 text-sm text-neutral-500">{t}</p>
                    </li>
                  ))}
                </ol>
                <Hint className="mt-4">
                  You can carry on without it: finished songs wait here until the 3DS asks. If Get Songs can’t find this PC, press Y
                  there and type {server?.lan?.length ? <b className="font-mono text-neutral-300">{server.lan.join(" or ")}</b> : "this PC’s address"}.
                </Hint>
              </>
            )}
          </StepCard>

          {/* 2 ------------------------------------------------------------------------------------------------- */}
          <StepCard id="step-song" n={2} title="Pick a song">
            <div className="mb-5 grid gap-3 sm:grid-cols-2" role="tablist">
              <ModeButton active={mode === "record"} onClick={() => setMode("record")} icon={<IconBrandChrome size={22} />}
                title="Record a browser tab" text="Play a song in another tab and capture its sound." />
              <ModeButton active={mode === "file"} onClick={() => setMode("file")} icon={<IconUpload size={22} />}
                title="Upload a file" text="MP3, M4A, FLAC, WAV, OGG…" />
            </div>
            {mode === "record" ? <TabRecorder onDone={pickSource} disabled={busy} /> : (
              <FileUpload accept={AUDIO_ACCEPT} title="Upload a song" subtitle="Drag a file here, or click to pick one"
                onChange={(files) => pickSource({ blob: files[0], name: files[0].name, url: URL.createObjectURL(files[0]), recorded: false })} />
            )}
            {source && (
              <div className="mt-5 rounded-xl border border-emerald-500/30 bg-emerald-500/5 p-4">
                <p className="mb-2 flex items-center gap-2 text-sm font-medium text-emerald-300">
                  <IconCheck size={16} stroke={3} />
                  {source.recorded ? `Recorded ${fmtSecs(source.seconds)}` : source.name} · {(source.blob.size / 1048576).toFixed(1)} MB
                </p>
                <audio src={source.url} controls className="w-full" />
              </div>
            )}
          </StepCard>

          {/* 3 ------------------------------------------------------------------------------------------------- */}
          <StepCard id="step-name" n={3} title="Name it">
            <div className="grid gap-4 sm:grid-cols-2">
              <Field label="Song title" value={title} onChange={setTitle} placeholder="Shown in BeatPad’s song list" id="title" />
              <Field label="Artist" hint="(optional)" value={artist} onChange={setArtist} placeholder="Who sings it" id="artist" />
            </div>
            <Hint className="mt-3">Using the same title again replaces that song, and the 3DS offers it as an update.</Hint>
          </StepCard>

          {/* 4 ------------------------------------------------------------------------------------------------- */}
          <StepCard id="step-convert" n={4} title="Make the charts">
            <ConvertPanel source={source} title={title} upload={upload} job={job} error={error} onConvert={convert} />
          </StepCard>

          {/* 5 ------------------------------------------------------------------------------------------------- */}
          <StepCard id="step-play" n={5} title="Play it on your 3DS">
            {job?.stage !== "done" ? (
              <Hint>Once the charts are made, this is where you’ll see your 3DS pick the song up.</Hint>
            ) : delivered ? (
              <Status kind="ok" icon={<IconDeviceGamepad2 size={18} />}>
                “{job.title}” is on your 3DS. Press B to leave Get Songs, pick it from the song list and press A. Have fun!
              </Status>
            ) : (
              <>
                <Status kind="wait">Waiting for your 3DS to download “{job.title}”…</Status>
                <ol className="mt-4 list-decimal space-y-1.5 pl-5 text-sm text-neutral-400">
                  <li>In BeatPad, press <b className="text-neutral-200">Y</b> to open Get Songs.</li>
                  <li>Highlight <b className="text-neutral-200">{job.title}</b>, marked <span className="rounded bg-emerald-500/20 px-1.5 py-0.5 text-xs font-bold text-emerald-300">NEW</span> or <span className="rounded bg-amber-500/20 px-1.5 py-0.5 text-xs font-bold text-amber-300">UPDATE</span>, and press <b className="text-neutral-200">A</b>.</li>
                  <li>Press <b className="text-neutral-200">B</b>, then pick it from the song list.</li>
                </ol>
              </>
            )}
            {job?.stage === "done" && (
              <button onClick={another} className="mt-5 inline-flex items-center gap-2 rounded-lg border border-neutral-700 px-4 py-2 text-sm font-medium text-neutral-200 transition hover:border-neutral-500 hover:bg-neutral-900">
                <IconPlus size={16} /> Add another song
              </button>
            )}
          </StepCard>
        </div>

        <aside className="flex flex-col gap-6 lg:sticky lg:top-6" aria-label="Library">
          <Library server={server} onDelete={remove} deleting={deleting} />
        </aside>
      </main>

      <Footer />
    </div>
  );
}

// ---------------------------------------------------------------------------------------------------------------
function ModeButton({ active, onClick, icon, title, text }) {
  return (
    <button type="button" role="tab" aria-selected={active} onClick={onClick}
      className={cn("flex items-start gap-3 rounded-xl border p-4 text-left transition",
        active ? "border-accent-500 bg-accent-500/10" : "border-neutral-800 hover:border-neutral-600")}>
      <span className={cn("mt-0.5", active ? "text-accent-400" : "text-neutral-500")}>{icon}</span>
      <span>
        <span className="block font-medium text-neutral-100">{title}</span>
        <span className="block text-sm text-neutral-500">{text}</span>
      </span>
    </button>
  );
}

function TabRecorder({ onDone, disabled }) {
  const [phase, setPhase] = useState("idle");        // idle | picking | waiting | recording
  const [secs, setSecs] = useState(0);
  const [quiet, setQuiet] = useState(0);
  const [err, setErr] = useState("");
  const [autoStop, setAutoStop] = useState(true);
  const [supported, setSupported] = useState(true);
  const ctl = useRef(null);
  const levels = useRef([]);
  const canvas = useRef(null);
  const auto = useRef(autoStop);
  auto.current = autoStop;

  useEffect(() => setSupported(canRecordTabs()), []);

  const draw = () => {
    const c = canvas.current;
    if (!c) return;
    const g = c.getContext("2d"), w = c.width, h = c.height, L = levels.current, N = 160, bw = w / N;
    g.clearRect(0, 0, w, h);
    L.forEach((db, i) => {
      const v = Math.max(0.02, Math.min(1, (db + 60) / 60)), x = (i + N - L.length) * bw;
      g.fillStyle = LANES[Math.floor((i + N - L.length) / 8) % 6];
      g.fillRect(x, (h - v * h) / 2, Math.max(1, bw - 2), v * h);
    });
  };

  const start = async () => {
    setErr(""); setPhase("picking"); levels.current = [];
    try {
      let frame = 0;
      ctl.current = await startTabRecording({
        onLevel: (db) => {
          if (++frame % 3) return;                                 // ~20 bars a second
          levels.current = [...levels.current.slice(-159), db];
          draw();
        },
        onTick: setSecs,
        onHeard: () => setPhase("recording"),
        onQuiet: setQuiet,
        autoStop: () => auto.current,
      });
      setPhase("waiting");
      const res = await ctl.current.done;
      setPhase("idle");
      if (res === null) return;                                     // cancelled
      if (!res.blob) return setErr("No sound was recorded. Was the song playing in the tab you shared?");
      onDone({ blob: res.blob, name: "recording.webm", url: URL.createObjectURL(res.blob), seconds: res.seconds, recorded: true });
    } catch (e) {
      setPhase("idle"); setErr(e.message);
    }
  };

  if (!supported) {
    return <Status kind="error">This browser can’t record tab audio. Open this page in Chrome, Chromium or Edge, or use “Upload a file”.</Status>;
  }

  if (phase === "idle" || phase === "picking") {
    return (
      <div>
        <ol className="mb-5 list-decimal space-y-1.5 pl-5 text-sm text-neutral-400">
          <li>Open the song in another tab and leave it <b className="text-neutral-200">paused at the start</b>.</li>
          <li>Click below, pick that <b className="text-neutral-200">tab</b>, and keep <b className="text-neutral-200">“Also share tab audio”</b> on.</li>
          <li>Press play there. You’ll keep hearing it while it records.</li>
        </ol>
        <div className="flex flex-wrap items-center gap-4">
          <HoverBorderGradient as="button" onClick={start} disabled={disabled || phase === "picking"}
            containerClassName="rounded-full" className="flex items-center gap-2 bg-neutral-950 px-5 py-2.5 text-sm font-semibold text-white">
            {phase === "picking" ? <IconLoader2 size={18} className="animate-spin" /> : <IconBrandChrome size={18} />}
            {phase === "picking" ? "Choose a tab in the browser’s window…" : "Choose a tab & record"}
          </HoverBorderGradient>
          <label className="flex items-center gap-2 text-sm text-neutral-400">
            <input type="checkbox" checked={autoStop} onChange={(e) => setAutoStop(e.target.checked)} />
            Stop by itself when the song ends
          </label>
        </div>
        {err && <Status kind="error" className="mt-4">{err}</Status>}
      </div>
    );
  }

  return (
    <div className="rounded-xl border border-neutral-800 bg-neutral-950/70 p-4">
      <div className="flex flex-wrap items-center gap-4">
        <span className="rec-dot h-3 w-3 rounded-full bg-red-500" />
        <span className="font-mono text-3xl font-bold tabular-nums text-white">{fmtSecs(secs)}</span>
        <span className="flex-1 text-sm text-neutral-400">
          {phase === "waiting" ? <span className="inline-flex items-center gap-2"><IconLoader2 size={16} className="animate-spin" />Waiting for sound: press play in the other tab</span>
            : quiet > 1.5 ? (autoStop ? `Quiet for ${quiet.toFixed(0)} s, stopping at 5 s…` : `Quiet for ${quiet.toFixed(0)} s`)
            : <span className="inline-flex items-center gap-2 text-neutral-200"><IconMusic size={16} className="text-accent-400" />Recording</span>}
        </span>
        <button onClick={() => ctl.current?.stop()} disabled={phase === "waiting"}
          className="inline-flex items-center gap-2 rounded-lg bg-red-500 px-4 py-2 text-sm font-semibold text-white transition hover:bg-red-400 disabled:opacity-40">
          <IconPlayerStopFilled size={16} /> Stop
        </button>
        <button onClick={() => ctl.current?.cancel()} className="inline-flex items-center gap-1.5 rounded-lg border border-neutral-700 px-3 py-2 text-sm text-neutral-300 hover:bg-neutral-900">
          <IconX size={16} /> Cancel
        </button>
      </div>
      <canvas ref={canvas} width={960} height={72} className="mt-4 h-[72px] w-full rounded-lg bg-neutral-900" />
    </div>
  );
}

const STAGES = [
  ["upload", "Uploading"],
  ["preparing", "Trimming the silence"],
  ["separating", "Separating the vocals"],
  ["charting", "Finding the beat & writing charts"],
  ["done", "Ready for your 3DS"],
];

function ConvertPanel({ source, title, upload, job, error, onConvert }) {
  const ready = !!source && !!title.trim();
  const stage = upload ? "upload" : job?.stage;
  if (!job && !upload) {
    return (
      <div>
        <ul className="mb-5 grid gap-1.5 text-sm">
          <Check ok={!!source}>A song {source ? `(${source.recorded ? "recording" : source.name})` : ""}</Check>
          <Check ok={!!title.trim()}>A title</Check>
        </ul>
        <HoverBorderGradient as="button" onClick={onConvert} disabled={!ready} containerClassName={cn("rounded-full", !ready && "opacity-40")}
          className="flex items-center gap-2 bg-neutral-950 px-6 py-2.5 text-sm font-semibold text-white">
          <IconSparkles size={18} /> Make the charts
        </HoverBorderGradient>
        <Hint className="mt-3">Takes 1 to 4 minutes, mostly splitting the singing from the music. You can leave this page open and start another song.</Hint>
        {error && <Status kind="error" className="mt-4">{error}</Status>}
      </div>
    );
  }
  if (job?.stage === "failed") {
    return (
      <div className="space-y-4">
        <Status kind="error">Couldn’t convert “{job.title}”: {job.error?.split("\n").pop()}</Status>
        <button onClick={onConvert} className="inline-flex items-center gap-2 rounded-lg border border-neutral-700 px-4 py-2 text-sm text-neutral-200 hover:bg-neutral-900">
          <IconRefresh size={16} /> Try again
        </button>
      </div>
    );
  }
  const idx = STAGES.findIndex(([k]) => k === (stage === "queued" ? "preparing" : stage));
  const pct = stage === "upload" ? upload.pct * 5 : stage === "queued" ? 5 : stage === "preparing" ? 8
    : stage === "separating" ? 10 + (job.pct || 0) * 0.8 : stage === "charting" ? 93 : 100;
  return (
    <div>
      <div className="mb-5 h-2 overflow-hidden rounded-full bg-neutral-800">
        <div className="h-full rounded-full bg-gradient-to-r from-lane-1 via-lane-4 to-lane-6 transition-[width] duration-500" style={{ width: `${pct}%` }} />
      </div>
      <ol className="grid gap-2.5">
        {STAGES.map(([k, label], i) => {
          const state = i < idx || stage === "done" ? "done" : i === idx ? "now" : "todo";
          const extra = k === "upload" && stage === "upload" ? ` ${Math.round(upload.pct * 100)}%`
            : k === "separating" && stage === "separating" && job?.pct != null ? ` ${job.pct}%`
            : k === "preparing" && stage === "queued" ? " (waiting for the song before it)" : "";
          return (
            <li key={k} className={cn("flex items-center gap-3 text-sm", state === "todo" ? "text-neutral-600" : state === "now" ? "text-white" : "text-neutral-400")}>
              <span className="grid h-6 w-6 shrink-0 place-items-center">
                {state === "done" ? <IconCheck size={18} stroke={3} className="text-emerald-400" />
                  : state === "now" ? <Spinner /> : <span className="h-2 w-2 rounded-full bg-neutral-700" />}
              </span>
              {label}{extra}
            </li>
          );
        })}
      </ol>
      {stage === "done" && job?.title && <Status kind="ok" className="mt-5">“{job.title}” is ready. Charts: Easy, Normal and Hard.</Status>}
    </div>
  );
}

function Library({ server, onDelete, deleting }) {
  const songs = server?.songs || [];
  const queue = (server?.jobs || []).filter((j) => !["done", "failed"].includes(j.stage));
  return (
    <section className="rounded-2xl border border-neutral-800 bg-neutral-900/60 p-5">
      <h2 className="mb-1 flex items-center gap-2 font-semibold text-neutral-100">
        <IconMusic size={18} className="text-accent-400" /> Your library
        <span className="ml-auto text-sm font-normal text-neutral-500">{songs.length} song{songs.length === 1 ? "" : "s"}</span>
      </h2>
      <Hint className="mb-4">Everything here is offered to your 3DS in Get Songs.</Hint>
      {!server && <Status kind="wait">Loading…</Status>}
      {queue.length > 0 && (
        <ul className="mb-3 grid gap-2">
          {queue.map((j) => (
            <li key={j.id} className="flex items-center gap-3 rounded-xl border border-accent-500/30 bg-accent-500/5 px-3 py-2.5">
              <Spinner />
              <div className="min-w-0 flex-1">
                <p className="truncate text-sm font-medium text-neutral-100">{j.title || "Reading tags…"}</p>
                <p className="text-xs text-neutral-500">{j.stage === "separating" ? `Separating vocals ${j.pct ?? 0}%` : j.stage === "charting" ? "Writing charts" : j.stage === "queued" ? "Waiting" : "Preparing"}</p>
              </div>
            </li>
          ))}
        </ul>
      )}
      {server && !songs.length && !queue.length && <p className="text-sm text-neutral-500">No songs yet. Your first one will appear here.</p>}
      <ul className="grid gap-2">
        {songs.map((s) => {
          const sent = server.sent?.[s.id];
          return (
            <li key={s.id} className="group flex items-center gap-3 rounded-xl border border-neutral-800 bg-neutral-950/60 px-3 py-2.5">
              <div className="min-w-0 flex-1">
                <p className="truncate text-sm font-medium text-neutral-100">{s.title}</p>
                <p className="truncate text-xs text-neutral-500">
                  {[s.artist, fmtTime(s.length), `${Math.round(s.bpm)} BPM`].filter(Boolean).join(" · ")}
                </p>
              </div>
              {sent && <span title={`Sent to ${sent.ip}`} className="rounded-md bg-emerald-500/15 px-1.5 py-0.5 text-[10px] font-bold uppercase tracking-wider text-emerald-400">On 3DS</span>}
              <button onClick={() => onDelete(s)} aria-label={`Delete ${s.title}`} disabled={deleting === s.id}
                className="rounded-md p-1.5 text-neutral-600 transition hover:bg-red-500/10 hover:text-red-400 disabled:opacity-60">
                {deleting === s.id ? <IconLoader2 size={16} className="animate-spin" /> : <IconTrash size={16} />}
              </button>
            </li>
          );
        })}
      </ul>
    </section>
  );
}

function Check({ ok, children }) {
  return (
    <li className={cn("flex items-center gap-2", ok ? "text-neutral-300" : "text-neutral-500")}>
      {ok ? <IconCheck size={16} stroke={3} className="text-emerald-400" /> : <span className="mx-[5px] h-1.5 w-1.5 rounded-full bg-neutral-600" />}
      {children}
    </li>
  );
}

function Field({ label, hint, value, onChange, placeholder, id }) {
  return (
    <label className="flex flex-col gap-1.5 text-sm font-medium text-neutral-300">
      <span>{label} {hint && <span className="font-normal text-neutral-500">{hint}</span>}</span>
      <input id={id} value={value} onChange={(e) => onChange(e.target.value)} placeholder={placeholder} maxLength={60} autoComplete="off"
        className="rounded-lg border border-neutral-800 bg-neutral-950 px-3 py-2.5 font-normal text-neutral-100 outline-none transition placeholder:text-neutral-600 focus:border-accent-500" />
    </label>
  );
}
