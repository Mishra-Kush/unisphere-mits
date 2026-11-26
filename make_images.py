"""Regenerates the local SVG artwork (no external images, nothing to license).
Run from the project folder:  python tools/make_images.py"""
import re, sqlite3, random, os, html
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
img = os.path.join(root, "assets", "images")
GRAD = {"technical": ("#1d4ed8", "#06b6d4"), "coding-ai": ("#4f46e5", "#a855f7"), "cultural": ("#be185d", "#f59e0b"),
        "music-dance": ("#db2777", "#7c3aed"), "sports-fitness": ("#059669", "#84cc16"), "arts-photography": ("#e83e9b", "#fb923c"),
        "entrepreneurship-career": ("#0f766e", "#3b82f6"), "environment-social": ("#15803d", "#14b8a6"),
        "research-innovation": ("#6d28d9", "#3b82f6"), "wellness-community": ("#0ea5e9", "#e83e9b")}

def hero():
    random.seed(7)
    beams = ""
    for i, (x, c, o) in enumerate([(160, "#3B82F6", .55), (420, "#E83E9B", .6), (700, "#38bdf8", .45), (980, "#E83E9B", .55), (1240, "#3B82F6", .5), (1480, "#a855f7", .45)]):
        tx = x + (i - 2.5) * 140
        beams += f'<polygon points="{x-6},0 {x+6},0 {tx+170},760 {tx-170},760" fill="{c}" opacity="{o*0.5}" filter="url(#b)"/>'
    crowd = ""
    for i in range(-1, 46):
        x = i * 36 + random.randint(-8, 8); h = random.randint(70, 150); y = 900 - h
        crowd += f'<ellipse cx="{x}" cy="{y}" rx="19" ry="21" fill="#07070b"/><rect x="{x-27}" y="{y+16}" width="54" height="{h+40}" rx="22" fill="#07070b"/>'
        if random.random() < .22:
            crowd += f'<rect x="{x+14}" y="{y-80}" width="11" height="90" rx="5" fill="#07070b" transform="rotate({random.randint(-25,25)} {x+20} {y})"/>'
    return f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1600 900" preserveAspectRatio="xMidYMid slice" role="img" aria-label="Stage lights above a cheering crowd">
<defs><linearGradient id="g" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#0b0b1a"/><stop offset=".6" stop-color="#1a0f2e"/><stop offset="1" stop-color="#2a0f3a"/></linearGradient>
<radialGradient id="s" cx=".5" cy=".62" r=".5"><stop offset="0" stop-color="#E83E9B" stop-opacity=".75"/><stop offset="1" stop-color="#E83E9B" stop-opacity="0"/></radialGradient>
<filter id="b"><feGaussianBlur stdDeviation="9"/></filter></defs>
<rect width="1600" height="900" fill="url(#g)"/>{beams}<rect width="1600" height="900" fill="url(#s)"/>
<g opacity=".9">{"".join(f'<circle cx="{random.randint(0,1600)}" cy="{random.randint(40,520)}" r="{random.choice([1.5,2,3])}" fill="#fff" opacity="{random.choice([.3,.5,.8])}"/>' for _ in range(70))}</g>
<g>{crowd}</g></svg>'''

def club_art(name, cat, n):
    a, b = GRAD.get(cat, ("#E83E9B", "#3B82F6"))
    words = re.findall(r"[A-Za-z]+", name.replace("MITS", "").replace("Club", ""))
    mono = ("".join(w[0] for w in words[:2]).upper() or name[:1])
    if not re.search(r"[A-Za-z]", name): mono = "ओ"
    rnd = random.Random(n)
    shapes = "".join(f'<circle cx="{rnd.randint(0,640)}" cy="{rnd.randint(0,400)}" r="{rnd.randint(30,110)}" fill="#fff" opacity="{rnd.choice([.05,.08,.12])}"/>' for _ in range(7))
    return f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 640 400" role="img" aria-label="{html.escape(name)}">
<defs><linearGradient id="g" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="{a}"/><stop offset="1" stop-color="{b}"/></linearGradient></defs>
<rect width="640" height="400" fill="url(#g)"/>{shapes}<rect width="640" height="400" fill="#000" opacity=".18"/>
<text x="320" y="248" text-anchor="middle" font-family="Bahnschrift,Segoe UI,Arial,sans-serif" font-weight="800" font-size="150" fill="#fff" opacity=".95">{mono}</text></svg>'''

open(os.path.join(img, "hero.svg"), "w", encoding="utf-8").write(hero())
open(os.path.join(img, "event-default.svg"), "w", encoding="utf-8").write(club_art("Event", "arts-photography", 99).replace(">E<", ">★<"))
open(os.path.join(root, "assets", "icons", "favicon.svg"), "w", encoding="utf-8").write('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64"><rect width="64" height="64" rx="14" fill="#111"/><circle cx="32" cy="32" r="18" fill="none" stroke="#E83E9B" stroke-width="6"/><circle cx="32" cy="32" r="6" fill="#3B82F6"/></svg>')
db = sqlite3.connect(os.path.join(root, "database", "_tmp.db"))
db.executescript(open(os.path.join(root, "database", "schema.sql"), encoding="utf-8").read() + open(os.path.join(root, "database", "seed.sql"), encoding="utf-8").read())
for i, (name, slug, cat) in enumerate(db.execute("select name, slug, category from clubs order by id"), 1):
    open(os.path.join(img, "clubs", slug + ".svg"), "w", encoding="utf-8").write(club_art(name, cat, i))
db.close(); os.remove(os.path.join(root, "database", "_tmp.db"))
print("images written:", len(os.listdir(os.path.join(img, "clubs"))), "club covers")
