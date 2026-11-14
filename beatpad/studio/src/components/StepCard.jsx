"use client";
import { GlowingEffect } from "@/components/ui/glowing-effect";
import { cn } from "@/lib/utils";

export default function StepCard({ id, n, title, aside, badge, children, className }) {
  return (
    <section id={id} className={cn("relative scroll-mt-20 rounded-2xl border border-neutral-800 p-1.5 xl:scroll-mt-6", className)}>
      <GlowingEffect spread={40} glow disabled={false} proximity={64} inactiveZone={0.01} borderWidth={2} />
      <div className="relative rounded-xl bg-neutral-900/70 p-5 backdrop-blur-sm sm:p-6">
        <h2 className="mb-4 flex items-center gap-3 text-base font-semibold text-neutral-100">
          <span className="grid h-7 w-7 shrink-0 place-items-center rounded-full bg-accent-500/15 text-sm font-bold text-accent-400">
            {n}
          </span>
          {title}
          {badge && (
            <span className="rounded-full border border-amber-500/50 px-2 py-0.5 text-[10px] font-bold uppercase tracking-wider text-amber-400">
              {badge}
            </span>
          )}
          {aside && <span className="ml-auto text-sm font-normal text-neutral-500 tabular-nums">{aside}</span>}
        </h2>
        {children}
      </div>
    </section>
  );
}

export function Hint({ children, className }) {
  return <p className={cn("text-sm text-neutral-500", className)}>{children}</p>;
}
