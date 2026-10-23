"""Polite scraper for tlidb.com that turns pages into JSON for the TorchOfBuilding engine.

Every page is fetched at most once and cached under data/raw/en/, so re-running parsers
costs nothing and a new season only needs `--refresh`. Requests are spaced out
(default 2 s) and identify the tool in the User-Agent.

Usage:
    python tlidb_scraper.py page Ice_Shot            # fetch + dump one page's parsed JSON
    python tlidb_scraper.py skills                   # all active skills -> data/json/active_skills.json
    python tlidb_scraper.py traits                   # all hero trait trees -> data/json/hero_traits.json
    python tlidb_scraper.py boards                   # all talent panels -> data/json/talent_boards.json
    python tlidb_scraper.py tables Talent            # generic table dump of any index page
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
import urllib.robotparser
from dataclasses import dataclass, field
from pathlib import Path

import requests
from bs4 import BeautifulSoup, Tag

BASE_URL = "https://tlidb.com"
LANG = "en"
USER_AGENT = "TorchOfBuilding-scraper/0.1 (offline TLI build planner; polite, cached)"

REPO_ROOT = Path(__file__).resolve().parents[2]
RAW_DIR = REPO_ROOT / "data" / "raw" / LANG
JSON_DIR = REPO_ROOT / "data" / "json"


# ---------------------------------------------------------------------------
# Fetching
# ---------------------------------------------------------------------------

@dataclass
class Fetcher:
    delay: float = 2.0
    refresh: bool = False
    _last: float = 0.0
    _session: requests.Session = field(default_factory=requests.Session)
    _robots: urllib.robotparser.RobotFileParser | None = None

    def __post_init__(self) -> None:
        self._session.headers["User-Agent"] = USER_AGENT

    def _allowed(self, url: str) -> bool:
        if self._robots is None:
            # Fetched with our session: RobotFileParser.read() uses urllib's default UA, which
            # Cloudflare rejects, and a 403 there is treated as "disallow everything".
            resp = self._session.get(f"{BASE_URL}/robots.txt", timeout=30)
            resp.raise_for_status()
            self._robots = urllib.robotparser.RobotFileParser()
            self._robots.parse(resp.text.splitlines())
        return self._robots.can_fetch(USER_AGENT, url)

    def get(self, slug: str) -> str:
        path = RAW_DIR / f"{slug}.html"
        if path.exists() and not self.refresh:
            return path.read_text(encoding="utf-8")

        url = f"{BASE_URL}/{LANG}/{slug}"
        if not self._allowed(url):
            raise RuntimeError(f"robots.txt disallows {url}")

        for attempt in range(5):
            wait = self.delay - (time.monotonic() - self._last)
            if wait > 0:
                time.sleep(wait)
            self._last = time.monotonic()
            resp = self._session.get(url, timeout=30)
            if resp.status_code == 200:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(resp.text, encoding="utf-8")
                return resp.text
            if resp.status_code in (429, 500, 502, 503, 504):
                backoff = self.delay * (2 ** (attempt + 1))
                print(f"  {resp.status_code} on {slug}, backing off {backoff:.0f}s", file=sys.stderr)
                time.sleep(backoff)
                continue
            raise RuntimeError(f"HTTP {resp.status_code} for {url}")
        raise RuntimeError(f"giving up on {url}")


# ---------------------------------------------------------------------------
# Generic helpers
# ---------------------------------------------------------------------------

def text_lines(el: Tag) -> list[str]:
    """Visible text of an element, one entry per <br>/<hr>-separated line."""
    el = BeautifulSoup(str(el), "html.parser")
    for br in el.find_all(["br", "hr"]):
        br.replace_with("\n")
    raw = el.get_text("", strip=False)
    return [re.sub(r"\s+", " ", ln).strip() for ln in raw.split("\n") if ln.strip()]


def card_header(card: Tag) -> str:
    header = card.find(class_="card-header")
    return re.sub(r"\s+", " ", header.get_text(" ", strip=True)) if header else ""


def parse_tables(html: str) -> list[dict]:
    """Every titled card table on a page: {header, columns, rows:[{col: {text, links}}]}."""
    soup = BeautifulSoup(html, "html.parser")
    out = []
    for table in soup.find_all("table"):
        card = table.find_parent(class_="card")
        columns = [th.get_text(" ", strip=True) for th in table.select("thead th")]
        rows = []
        for tr in table.select("tbody tr"):
            cells = tr.find_all("td")
            row = {}
            for i, td in enumerate(cells):
                col = columns[i] if i < len(columns) else f"col{i}"
                row[col] = {
                    "text": text_lines(td),
                    "links": [a["href"] for a in td.find_all("a", href=True)],
                }
            rows.append(row)
        out.append({"header": card_header(card) if card else "", "columns": columns, "rows": rows})
    return out


def _is_entry_link(href: str) -> bool:
    return not href.startswith(("http", "/", "#"))


def index_links(html: str) -> list[str]:
    """Entry slugs listed on an index page.

    tlidb uses two layouts: tables (first link per row) and card lists
    (`div.flex-grow-1 > a`, e.g. the Hero page).
    """
    slugs: list[str] = []
    tables = parse_tables(html)
    for t in tables:
        for row in t["rows"]:
            for cell in row.values():
                links = [l for l in cell["links"] if _is_entry_link(l)]
                if links:
                    slugs.append(links[0])
                    break
    if not slugs:
        soup = BeautifulSoup(html, "html.parser")
        for body in soup.select("div.d-flex > div.flex-grow-1"):
            a = body.find("a", href=True, recursive=False)
            if a and _is_entry_link(a["href"]):
                slugs.append(a["href"])
    return list(dict.fromkeys(slugs))


# ---------------------------------------------------------------------------
# Page parsers
# ---------------------------------------------------------------------------

PCT = re.compile(r"(-?\d+(?:\.\d+)?)%")


def parse_skill(slug: str, html: str) -> dict:
    soup = BeautifulSoup(html, "html.parser")
    card = soup.select_one("div.card.popupItem")
    if card is None:
        raise ValueError(f"{slug}: no skill card found")

    ver = card.select_one(".item_ver")
    skill: dict = {
        "slug": slug,
        "name": card.select_one(".card-title").get_text(strip=True),
        "season": ver.get_text(strip=True) if ver else "",
        "tags": [s.get_text(strip=True) for s in card.select("span.tag")],
        "stats": {},
        "description": [],
    }

    restrict = card.select_one('[data-block="weapon_restrict_description"]')
    if restrict:
        skill["restriction"] = restrict.get_text(strip=True)

    for row in card.select("div.d-flex.justify-content-center"):
        parts = [d.get_text(strip=True) for d in row.find_all("div", recursive=False)]
        if len(parts) == 2:
            skill["stats"][parts[0].rstrip(":")] = parts[1]

    # The last explicitMod blocks are the "Details" view; keep all for now.
    for mod in card.select("div.explicitMod"):
        skill["description"].append(text_lines(mod))

    info = soup.find(string=re.compile(r"^\s*id:\s*\d+"))
    if info:
        skill["id"] = int(re.search(r"\d+", info).group())

    levels = []
    for table in soup.find_all("table"):
        heads = [th.get_text(strip=True).lower() for th in table.select("thead th")]
        if heads[:1] != ["level"]:
            continue
        for tr in table.select("tbody tr"):
            tds = tr.find_all("td")
            if len(tds) < 2:
                continue
            entry: dict = {"level": int(tds[0].get_text(strip=True))}
            eff = tds[1].select_one('[data-source="damage_effective"]')
            if eff and (m := PCT.search(eff.get_text())):
                entry["added_damage_effectiveness"] = float(m.group(1))
            des = tds[1].select_one('[data-source="des"]')
            entry["text"] = text_lines(des or tds[1])
            levels.append(entry)
        break
    skill["levels"] = levels
    return skill


def parse_hero_trait(slug: str, html: str) -> dict:
    """Hero trait page: every trait node with its required level and per-level modifier text.

    Nodes sharing a required level are the options of one choice tier.
    """
    soup = BeautifulSoup(html, "html.parser")
    nodes = []
    for name_el in soup.select("div.fw-bold[data-id]"):
        body = name_el.parent
        req = re.search(r"Require lv\s*(\d+)", body.get_text(" ", strip=True))
        levels = []
        for tier in body.select("[data-tier-content]"):
            levels.append({
                "level": int(tier["data-tier-content"]),
                "affix": [ln for el in tier.select('[data-src="affix"]') for ln in text_lines(el)],
                "server_affix": [ln for el in tier.select('[data-src="server_affix"]') for ln in text_lines(el)],
            })
        nodes.append({
            "id": int(name_el["data-id"]),
            "name": name_el.get_text(strip=True),
            "required_level": int(req.group(1)) if req else None,
            "levels": levels,
        })

    tiers: dict[int, list[str]] = {}
    for n in nodes:
        if n["required_level"] is not None:
            tiers.setdefault(n["required_level"], []).append(n["name"])

    # Some heroes (e.g. Youga: The Ultimate One) use a trait tree instead of level tiers. The tree
    # SVG joins nodes with lines whose data-id is "c<parent id>-<child id>".
    tree_links = []
    tree = soup.select_one("svg#passive_skill_tree")
    if tree:
        for line in tree.select("line[data-id]"):
            m = re.fullmatch(r"c(\d+)-(\d+)", line["data-id"])
            if m:
                tree_links.append({"parent": int(m.group(1)), "child": int(m.group(2))})

    portrait_link = soup.select_one("div.card-body div.flex-grow-1 > a[href]")
    return {
        "slug": slug,
        "hero": portrait_link.get_text(strip=True) if portrait_link else "",
        "nodes": nodes,
        "tiers": {str(k): v for k, v in sorted(tiers.items())},
        "tree_links": tree_links,
    }


BOARD_X0, BOARD_DX = 96, 128      # node centre x for column 0, spacing between columns
BOARD_Y0, BOARD_DY = 80, 96       # node centre y for row 0, spacing between rows
POINTS_PER_COLUMN = 3             # column c needs 3*c points spent in the panel


def _tooltip(el: Tag) -> tuple[str, list[str]]:
    """Name and modifier lines from a board node's data-bs-title tooltip HTML."""
    tip = BeautifulSoup(el.get("data-bs-title", ""), "html.parser")
    name_el = tip.select_one(".sp-node-name")
    attr = tip.select_one('[data-source="attr"]')
    return (name_el.get_text(strip=True) if name_el else "", text_lines(attr) if attr else [])


def parse_talent_board(slug: str, html: str) -> dict:
    """A god/goddess talent panel: core talent options and the 7x5 node grid.

    tlidb draws each panel as an SVG. Core talents sit above the grid (y < 0) under a text label
    giving the panel points needed. Grid nodes sit at (BOARD_X0 + col*BOARD_DX, BOARD_Y0 + row*BOARD_DY)
    with their max points written at (cx+22, cy+26). Lines join a node to its prerequisite on the left.
    """
    soup = BeautifulSoup(html, "html.parser")
    svg = next((s for s in soup.find_all("svg") if s.select_one("g.nodes")), None)
    if svg is None:
        raise ValueError(f"{slug}: no talent board svg")

    texts = [(float(t["x"]), float(t["y"]), t.get_text(strip=True)) for t in svg.select("text.level_up_time")]
    core_labels = sorted((x, int(v)) for x, y, v in texts if y < 0 and v.isdigit())
    point_labels = {(round(x), round(y)): int(v) for x, y, v in texts if y >= 0 and v.isdigit()}

    core = []
    for img in svg.select("g.nodes > image"):
        x, y = float(img["x"]), float(img["y"])
        if y >= 0:
            continue
        name, mods = _tooltip(img)
        label = [pts for lx, pts in core_labels if lx <= x + 32]
        core.append({"name": name, "mods": mods, "required_points": label[-1] if label else None})

    nodes = []
    for circle in svg.select("g.nodes > circle.talent_bg"):
        cx, cy = float(circle["cx"]), float(circle["cy"])
        col = round((cx - BOARD_X0) / BOARD_DX)
        row = round((cy - BOARD_Y0) / BOARD_DY)
        img = circle.find_next_sibling("image")
        name, mods = _tooltip(img) if img else ("", [])
        kind = next((c.replace("talent_type", "") for c in circle.get("class", []) if c.startswith("talent_type")), "")
        nodes.append({
            "col": col,
            "row": row,
            "required_points": col * POINTS_PER_COLUMN,
            "max_points": point_labels.get((round(cx + 22), round(cy + 26))),
            "type": int(kind) if kind.isdigit() else None,
            "name": name,
            "mods": mods,
        })

    links = []
    for line in svg.select("g.connections > line"):
        x1, y1, x2, y2 = (float(line[a]) for a in ("x1", "y1", "x2", "y2"))
        # Lines run from the right node's left edge (x1) back to the left node's right edge (x2).
        to = (round((x1 + 32 - BOARD_X0) / BOARD_DX), round((y1 - 2 - BOARD_Y0) / BOARD_DY))
        frm = (round((x2 - 32 - BOARD_X0) / BOARD_DX), round((y2 - 2 - BOARD_Y0) / BOARD_DY))
        links.append({"from": list(frm), "to": list(to)})

    header = soup.select_one(".card-header")
    return {
        "slug": slug,
        "name": header.get_text(" ", strip=True).split("/")[0].strip() if header else slug,
        "core_talents": core,
        "nodes": sorted(nodes, key=lambda n: (n["col"], n["row"])),
        "links": links,
    }


# ---------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------

def write_json(name: str, data) -> Path:
    JSON_DIR.mkdir(parents=True, exist_ok=True)
    path = JSON_DIR / name
    path.write_text(json.dumps(data, indent=1, ensure_ascii=False), encoding="utf-8")
    print(f"wrote {path} ({len(data) if hasattr(data, '__len__') else 1} entries)")
    return path


def cmd_page(f: Fetcher, slug: str) -> None:
    html = f.get(slug)
    soup = BeautifulSoup(html, "html.parser")
    if soup.select_one("div.card.popupItem"):
        data = parse_skill(slug, html)
    elif soup.select_one("div.fw-bold[data-id]"):
        data = parse_hero_trait(slug, html)
    else:
        data = {"slug": slug, "tables": parse_tables(html)}
    print(json.dumps(data, indent=1, ensure_ascii=False))


def cmd_skills(f: Fetcher, index_slug: str, out_name: str) -> None:
    slugs = index_links(f.get(index_slug))
    print(f"{index_slug}: {len(slugs)} entries")
    skills, failures = [], []
    for i, slug in enumerate(slugs, 1):
        print(f"[{i}/{len(slugs)}] {slug}")
        try:
            skills.append(parse_skill(slug, f.get(slug)))
        except Exception as e:  # keep going; report at the end
            failures.append({"slug": slug, "error": str(e)})
    write_json(out_name, skills)
    if failures:
        write_json(out_name.replace(".json", "_failures.json"), failures)


def cmd_traits(f: Fetcher) -> None:
    # Trait entries on the Hero page are labelled "<Hero> | <Trait>".
    soup = BeautifulSoup(f.get("Hero"), "html.parser")
    slugs = list(dict.fromkeys(
        a["href"] for a in soup.select("div.flex-grow-1 > a[href]")
        if " | " in a.get_text() and _is_entry_link(a["href"])
    ))
    print(f"Hero: {len(slugs)} trait pages")
    traits = []
    for i, slug in enumerate(slugs, 1):
        print(f"[{i}/{len(slugs)}] {slug}")
        traits.append(parse_hero_trait(slug, f.get(slug)))
    write_json("hero_traits.json", traits)


def cmd_skillpages(f: Fetcher, slugs: list[str]) -> None:
    """Parse the given skill/support pages and merge them into data/json/skills.json (keyed by name)."""
    path = JSON_DIR / "skills.json"
    existing = json.loads(path.read_text(encoding="utf-8")) if path.exists() else {}
    for slug in slugs:
        s = parse_skill(slug, f.get(slug))
        existing[s["name"]] = s
        print(f"  {s['name']}: {len(s['levels'])} levels")
    write_json("skills.json", existing)


def parse_alternate_youga(html: str) -> dict:
    """Alternate Spacetime Youga: per-skill bonus lines granted while Selves of All Realms is active."""
    soup = BeautifulSoup(html, "html.parser")
    effects: dict[str, list[str]] = {}
    for body in soup.select("div.d-flex > div.flex-grow-1"):
        # Each entry is a skill-name link followed by its effect text (one line per <br>).
        name_el = body.find("a")
        if not name_el:
            continue
        name = name_el.get_text(strip=True)
        name_el.extract()
        lines = text_lines(body)
        if lines:
            effects[name] = lines
    return effects


SKILL_INDEXES = [
    "Active_Skill", "Support_Skill", "Passive_Skill", "Activation_Medium_Skill",
    "Magnificent_Support_Skill", "Noble_Support_Skill", "Modularization_Skill",
    "Tartarus_Skill", "Origin_Tartarus_Skill",
]


def parse_legendary(slug: str, html: str) -> dict:
    """Legendary gear page: one modifier list per variant card ("" = normal, "Corroded", ...)."""
    soup = BeautifulSoup(html, "html.parser")
    h1 = soup.find("h1")
    variants: dict[str, list[str]] = {}
    for card in soup.select("div.card"):
        spans = card.select("span[data-modifier-id]")
        if not spans:
            continue
        header = card_header(card) or "Normal"
        if header in variants:
            continue
        variants[header] = [ln for s in spans for ln in text_lines(s)]
    return {"slug": slug, "name": h1.get_text(strip=True) if h1 else slug, "variants": variants}


def cmd_bulk(f: Fetcher, kind: str, dry_run: bool) -> None:
    """Every entry of the skill indexes ("skills") or the Legendary Gear index ("legendaries")."""
    indexes = SKILL_INDEXES if kind == "skills" else ["Legendary_Gear"]
    slugs: list[str] = []
    for idx in indexes:
        found = index_links(f.get(idx))
        print(f"{idx}: {len(found)} entries")
        slugs.extend(found)
    slugs = list(dict.fromkeys(slugs))
    print(f"total unique: {len(slugs)} (about {len(slugs) * f.delay / 60:.0f} min uncached)")
    if dry_run:
        return

    out_name = "skills.json" if kind == "skills" else "legendaries.json"
    path = JSON_DIR / out_name
    existing = json.loads(path.read_text(encoding="utf-8")) if path.exists() and kind == "skills" else {}
    results, failures = dict(existing), []
    for i, slug in enumerate(slugs, 1):
        if i % 25 == 0:
            print(f"[{i}/{len(slugs)}] {slug}", flush=True)
        try:
            html = f.get(slug)
            entry = parse_skill(slug, html) if kind == "skills" else parse_legendary(slug, html)
            results[entry["name"]] = entry
        except Exception as e:  # keep going; report at the end
            failures.append({"slug": slug, "error": str(e)})
    write_json(out_name, results)
    write_json(out_name.replace(".json", "_failures.json"), failures)


def cmd_boards(f: Fetcher) -> None:
    # The Talent page's "Profession" tab lists every god/goddess panel as a card list.
    soup = BeautifulSoup(f.get("Talent"), "html.parser")
    pane = soup.select_one("div#Profession")
    slugs = list(dict.fromkeys(
        a["href"] for a in (pane.select("div.flex-grow-1 > a[href]") if pane else [])
        if _is_entry_link(a["href"])
    ))
    print(f"Talent: {len(slugs)} boards")
    boards, failures = [], []
    for i, slug in enumerate(slugs, 1):
        print(f"[{i}/{len(slugs)}] {slug}")
        try:
            boards.append(parse_talent_board(slug, f.get(slug)))
        except Exception as e:
            failures.append({"slug": slug, "error": str(e)})
    write_json("talent_boards.json", boards)
    if failures:
        write_json("talent_boards_failures.json", failures)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--delay", type=float, default=2.0, help="seconds between requests (default 2)")
    ap.add_argument("--refresh", action="store_true", help="ignore the cache and re-download")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("page"); p.add_argument("slug")
    sub.add_parser("skills")
    sub.add_parser("traits")
    sub.add_parser("boards")
    sp = sub.add_parser("skillpages"); sp.add_argument("slugs", nargs="+")
    sub.add_parser("altyouga")
    for kind in ("skills-all", "legendaries"):
        b = sub.add_parser(kind)
        b.add_argument("--dry-run", action="store_true", help="only count entries")
    t = sub.add_parser("tables"); t.add_argument("slug")
    args = ap.parse_args(argv)

    f = Fetcher(delay=max(args.delay, 1.0), refresh=args.refresh)
    if args.cmd == "page":
        cmd_page(f, args.slug)
    elif args.cmd == "skills":
        cmd_skills(f, "Active_Skill", "active_skills.json")
    elif args.cmd == "traits":
        cmd_traits(f)
    elif args.cmd == "boards":
        cmd_boards(f)
    elif args.cmd == "skillpages":
        cmd_skillpages(f, args.slugs)
    elif args.cmd in ("skills-all", "legendaries"):
        cmd_bulk(f, "skills" if args.cmd == "skills-all" else "legendaries", args.dry_run)
    elif args.cmd == "altyouga":
        write_json("alternate_spacetime_youga.json", parse_alternate_youga(f.get("Alternate_Spacetime_Youga")))
    elif args.cmd == "tables":
        write_json(f"tables_{args.slug}.json", parse_tables(f.get(args.slug)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
