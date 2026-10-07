"""Render authentic terminal logs into report images (not desktop screenshots)."""
import argparse
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

root = Path(__file__).resolve().parent.parent
font_path = Path("C:/Windows/Fonts/consola.ttf")
font = ImageFont.truetype(str(font_path), 18)
report_dir = root.parent / "report"
image_dir = report_dir / "images"
image_dir.mkdir(parents=True, exist_ok=True)
for source, target, command in [("build.log", "build.png", "make -B -j2"),
                                ("qemu.log", "qemu.png", "make qemu"),
                                ("grade.log", "test_result.png", "make grade")]:
    text = (report_dir / "validation" / source).read_text(encoding="utf-8", errors="replace")
    lines = text.expandtabs(4).splitlines()
    # Full output is preserved; size follows the longest line and total line count.
    width = max(1040, int(max([font.getlength(line) for line in lines] + [0])) + 64)
    height = 100 + len(lines) * 24
    canvas = Image.new("RGB", (width, height), "#101820")
    draw = ImageDraw.Draw(canvas)
    draw.rectangle((0, 0, width, 58), fill="#1f2d3b")
    draw.text((24, 19), f"lab1 | $ {command} | captured output", font=font, fill="#eaf2f8")
    for index, line in enumerate(lines):
        color = "#8be9aa" if "PASS" in line else "#d7e0e8"
        draw.text((24, 78 + index * 24), line, font=font, fill=color)
    canvas.save(image_dir / target)
    print(f"Rendered {source} -> ../report/images/{target} ({width}x{height})")
