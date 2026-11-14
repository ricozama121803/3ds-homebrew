"use client";
import { useEffect, useState } from "react";
import { IconCheck } from "@tabler/icons-react";
import { cn } from "@/lib/utils";

/**
 * Sticky step navigation with scroll-spy.
 * steps: [{ id, n, label, done }]
 * variant "rail": vertical, for wide screens; "bar": horizontal chips, for small ones.
 */
export function useActiveStep(ids) {
  const [active, setActive] = useState(ids[0]);
  useEffect(() => {
    const visible = new Map();
    const observer = new IntersectionObserver(
      (entries) => {
        for (const e of entries) visible.set(e.target.id, e.isIntersecting ? e.intersectionRatio : 0);
        // The active step is the first one (in page order) that's on screen.
        const first = ids.find((id) => (visible.get(id) ?? 0) > 0);
        if (first) setActive(first);
      },
      { rootMargin: "-20% 0px -55% 0px", threshold: [0, 0.01, 0.5, 1] },
    );
    for (const id of ids) {
      const el = document.getElementById(id);
      if (el) observer.observe(el);
    }
    return () => observer.disconnect();
  }, [ids]);
  return [active, setActive];
}

const jump = (id, setActive) => {
  const el = document.getElementById(id);
  if (!el) return;
  const reduce = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  el.scrollIntoView({ behavior: reduce ? "auto" : "smooth", block: "start" });
  setActive(id);
};

export default function StepNav({ steps, active, setActive, variant }) {
  const index = Math.max(0, steps.findIndex((s) => s.id === active));
  const doneCount = steps.filter((s) => s.done).length;

  if (variant === "bar") {
    return (
      <nav aria-label="Steps" className="sticky top-0 z-30 -mx-4 mb-2 border-b border-neutral-800/80 bg-neutral-950/85 px-4 py-2 backdrop-blur sm:-mx-6 sm:px-6 xl:hidden">
        <ol className="flex gap-1.5 overflow-x-auto [scrollbar-width:none]">
          {steps.map((s) => (
            <li key={s.id} className="shrink-0">
              <button
                type="button" onClick={() => jump(s.id, setActive)}
                aria-current={s.id === active ? "step" : undefined}
                className={cn(
                  "flex items-center gap-1.5 rounded-full border px-3 py-1.5 text-xs font-medium transition",
                  s.id === active ? "border-accent-500 bg-accent-500/15 text-white" : "border-neutral-800 text-neutral-400 hover:text-neutral-200",
                )}
              >
                <StepDot s={s} active={s.id === active} small />
                {s.label}
              </button>
            </li>
          ))}
        </ol>
      </nav>
    );
  }

  return (
    <nav aria-label="Steps" className="sticky top-6 hidden xl:block">
      <p className="mb-3 text-[11px] font-semibold uppercase tracking-[0.2em] text-neutral-500">
        Steps <span className="ml-1 font-normal normal-case tracking-normal text-neutral-600">{doneCount}/{steps.length} done</span>
      </p>
      <div className="relative">
        {/* progress track */}
        <div className="absolute bottom-3 left-[13px] top-3 w-px bg-neutral-800" />
        <div
          className="absolute left-[13px] top-3 w-px bg-gradient-to-b from-accent-500 to-[#4f8cff] transition-[height] duration-300"
          style={{ height: `calc((100% - 1.5rem) * ${index / Math.max(1, steps.length - 1)})` }}
        />
        <ol className="relative space-y-1">
          {steps.map((s) => {
            const isActive = s.id === active;
            return (
              <li key={s.id}>
                <button
                  type="button" onClick={() => jump(s.id, setActive)}
                  aria-current={isActive ? "step" : undefined}
                  className={cn(
                    "group flex w-full items-center gap-3 rounded-lg py-1.5 pr-2 text-left text-sm transition",
                    isActive ? "text-white" : "text-neutral-500 hover:text-neutral-200",
                  )}
                >
                  <StepDot s={s} active={isActive} />
                  <span className={cn("truncate", isActive && "font-semibold")}>{s.label}</span>
                </button>
              </li>
            );
          })}
        </ol>
      </div>
    </nav>
  );
}

function StepDot({ s, active, small }) {
  return (
    <span
      className={cn(
        "grid shrink-0 place-items-center rounded-full border font-bold tabular-nums transition",
        small ? "h-4 w-4 text-[9px]" : "h-[27px] w-[27px] text-[11px]",
        s.done
          ? "border-emerald-500/60 bg-emerald-500/15 text-emerald-400"
          : active
            ? "border-accent-500 bg-accent-500 text-white shadow-[0_0_12px_rgba(124,77,255,0.6)]"
            : "border-neutral-700 bg-neutral-950 text-neutral-500 group-hover:border-neutral-500",
      )}
    >
      {s.done ? <IconCheck size={small ? 10 : 14} stroke={3} /> : s.n}
    </span>
  );
}
