"use client";
import { IconBrandGithub, IconBrandLinkedin, IconCode, IconWorld } from "@tabler/icons-react";
import { FloatingDock } from "@/components/ui/floating-dock";

const LINKS = [
  { title: "GitHub", href: "https://github.com/ricozama121803", icon: <IconBrandGithub className="h-full w-full text-neutral-300" /> },
  { title: "LinkedIn", href: "https://www.linkedin.com/in/enrico-zamarripa-3ba2882a3/", icon: <IconBrandLinkedin className="h-full w-full text-neutral-300" /> },
  { title: "Portfolio", href: "https://enrico-zamarripa-portfolio-site.vercel.app", icon: <IconWorld className="h-full w-full text-neutral-300" /> },
  { title: "Source code", href: "https://github.com/ricozama121803/3ds-homebrew/tree/main/beatpad", icon: <IconCode className="h-full w-full text-neutral-300" /> },
];

export default function Footer() {
  return (
    <footer className="relative z-10 mt-16 border-t border-neutral-800/80 bg-neutral-950/80">
      <div className="mx-auto flex max-w-7xl flex-col items-center gap-6 px-4 py-10 sm:flex-row sm:justify-between">
        <div className="text-center sm:text-left">
          <p className="font-semibold text-neutral-200">BeatPad Song Studio</p>
          <p className="mt-1 text-sm text-neutral-500">
            Built by{" "}
            <a href="https://enrico-zamarripa-portfolio-site.vercel.app" target="_blank" rel="noopener noreferrer" className="text-neutral-300 underline-offset-4 hover:underline">
              Enrico Zamarripa
            </a>
            . Vocals are separated with Demucs; everything runs on this PC.
          </p>
        </div>
        <FloatingDock items={LINKS} desktopClassName="mx-0 bg-neutral-900" mobileClassName="hidden" />
        <ul className="flex gap-3 md:hidden">
          {LINKS.map((l) => (
            <li key={l.title}>
              <a href={l.href} target="_blank" rel="noopener noreferrer" aria-label={l.title}
                className="flex h-11 w-11 items-center justify-center rounded-full bg-neutral-800 p-2.5 transition hover:bg-neutral-700">
                {l.icon}
              </a>
            </li>
          ))}
        </ul>
      </div>
      <p className="pb-8 text-center text-xs text-neutral-600">Not affiliated with Nintendo. BeatPad needs a 3DS with custom firmware.</p>
    </footer>
  );
}
