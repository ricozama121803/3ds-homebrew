// BeatPad's note gem: bobs to a 130 BPM-ish beat and blinks.
export default function Mascot({ className }) {
  return (
    <svg viewBox="0 0 120 120" className={className} role="img" aria-label="BeatPad note gem">
      <ellipse className="gem-shadow" cx="60" cy="108" rx="30" ry="5" fill="#000" style={{ transformOrigin: "60px 108px" }} />
      <g className="gem-body">
        <ellipse cx="60" cy="66" rx="50" ry="30" fill="#3b1d8f" />
        <ellipse cx="60" cy="60" rx="48" ry="28" fill="#5a2fd0" />
        <ellipse cx="60" cy="55" rx="42" ry="22" fill="#7c4dff" />
        <ellipse cx="46" cy="44" rx="18" ry="6" fill="#c9b8ff" opacity="0.85" />
        <ellipse className="gem-eye" cx="47" cy="58" rx="6" ry="7.5" fill="#fff" />
        <ellipse className="gem-eye" cx="73" cy="58" rx="6" ry="7.5" fill="#fff" />
        <circle cx="48.5" cy="60" r="3.2" fill="#14141c" />
        <circle cx="74.5" cy="60" r="3.2" fill="#14141c" />
        <path d="M52 72 Q60 79 68 72" stroke="#14141c" strokeWidth="3" fill="none" strokeLinecap="round" />
        <circle cx="38" cy="70" r="4" fill="#ff7ab0" opacity="0.55" />
        <circle cx="82" cy="70" r="4" fill="#ff7ab0" opacity="0.55" />
      </g>
    </svg>
  );
}
