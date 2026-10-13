"""Labelled hero inspection sheets at the 3DS's 400x240 screen resolution."""
import sys
from pathlib import Path
from PIL import Image, ImageDraw

out = Path(sys.argv[1])
groups = {
    "run": [f"f{i}_run" for i in range(8)],
    "walk": [f"g{i}_walk" for i in range(8)],
    "start": [f"gs{i}_start" for i in range(6)],
    "stop": [f"gs{i}_stop" for i in range(8)],
    "poses": ["h1_swing", "h2_run", "h2b_run", "h3_idle", "h4_jump", "h5_climb", "k5_crouch", "w2_wing_side"],
    "air": ["k0_trick", "k1_trick", "k2_trick", "k3_trick", "k6a_air", "k6b_air", "k6c_air", "w4_wing_above"],
    "wall": [f"wr{i}_wallrun" for i in range(6)],
    "roll": [f"r{i}_roll" for i in range(14)],
}
for t in range(1, 6):
    groups[f"trick{t}"] = [f"t{t}_{i}_trick" for i in range(8)]
for label, names in groups.items():
    sheet = Image.new("RGB", (1600, 264 * ((len(names) + 3) // 4)), "#dddddd")
    draw = ImageDraw.Draw(sheet)
    for i, name in enumerate(names):
        x, y = i % 4 * 400, i // 4 * 264
        with Image.open(out / f"{name}.png") as frame:
            sheet.paste(frame.convert("RGB").resize((400, 240), Image.Resampling.LANCZOS), (x, y + 24))
        draw.text((x + 8, y + 6), name, fill="black")
    sheet.save(out / f"sheet_{label}.png")
