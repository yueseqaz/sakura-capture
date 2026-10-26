"""Regenerate the original geometric app icon; requires Pillow only at development time."""
from pathlib import Path
from PIL import Image, ImageDraw

root = Path(__file__).resolve().parents[1]
image = Image.new("RGBA", (256, 256), (0, 0, 0, 0))
draw = ImageDraw.Draw(image)
draw.rounded_rectangle((8, 8, 248, 248), radius=58, fill="#e05e72")
for points in [((63, 101), (63, 63), (101, 63)),
               ((155, 63), (193, 63), (193, 101)),
               ((63, 155), (63, 193), (101, 193)),
               ((155, 193), (193, 193), (193, 155))]:
    draw.line(points, fill="#fff9ed", width=14, joint="curve")
draw.ellipse((101, 101, 155, 155), fill="#fff9ed")
image.save(root / "assets/icon.png")
image.save(root / "assets/icon.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
