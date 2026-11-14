import { IconLoader2, IconCheck, IconAlertTriangle } from "@tabler/icons-react";
import { cn } from "@/lib/utils";

export function Spinner({ size = 18, className }) {
  return <IconLoader2 size={size} className={cn("animate-spin text-accent-400", className)} aria-label="Working" />;
}

// One-line status: kind "wait" (spinner), "ok" (check), "error" (warning)
export function Status({ kind, icon, children, className }) {
  const styles = {
    wait: "border-accent-500/30 bg-accent-500/5 text-neutral-200",
    ok: "border-emerald-500/30 bg-emerald-500/5 text-emerald-200",
    error: "border-red-500/40 bg-red-500/10 text-red-200",
  }[kind];
  const mark = icon || (kind === "wait" ? <Spinner /> : kind === "ok" ? <IconCheck size={18} stroke={3} className="text-emerald-400" />
    : <IconAlertTriangle size={18} className="text-red-400" />);
  return (
    <div role={kind === "error" ? "alert" : "status"} className={cn("flex items-start gap-3 rounded-xl border px-4 py-3 text-sm", styles, className)}>
      <span className="mt-px shrink-0">{mark}</span>
      <div>{children}</div>
    </div>
  );
}
