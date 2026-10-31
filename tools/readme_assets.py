#!/usr/bin/env python3
"""Draws the README's figures into docs/assets/*.svg in one skin: dark teal panels taken from the
logo, monospace lettering converted to paths (GitHub reads these through <img>, so no font may be
assumed), hairline rules, one accent.

    py -3 tools/readme_assets.py            writes badge-*.svg, banner.svg, h-*.svg, stack.svg, flow.svg, host.svg

Needs fontTools and a monospace TrueType font on the machine (Cascadia Mono or Consolas on
Windows, DejaVu Sans Mono on Linux); the output is committed, so a reader needs nothing.
"""
from __future__ import annotations

import base64
import html
import os
import sys

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, "docs", "assets")

BG = "#07171a"
PANEL = "#0b2124"
PANEL2 = "#10292d"
LINE = "#1d4448"
INK = "#d8e9e8"
MUTED = "#7aa3a1"
TEAL = "#17b3ac"
DEEP = "#035d60"

W = 1280

FONT_CANDIDATES = {
    "regular": ["CascadiaMono.ttf", "consola.ttf", "DejaVuSansMono.ttf"],
    "bold": ["consolab.ttf", "CascadiaMono.ttf", "DejaVuSansMono-Bold.ttf"],
}
FONT_DIRS = [os.path.join(os.environ.get("WINDIR", "C:/Windows"), "Fonts"),
             "/usr/share/fonts/truetype/dejavu", "/usr/share/fonts/TTF"]


class Face:
    def __init__(self, path: str):
        self.font = TTFont(path)
        self.cmap = self.font.getBestCmap()
        self.glyphs = self.font.getGlyphSet()
        self.hmtx = self.font["hmtx"]
        self.upm = self.font["head"].unitsPerEm

    def path(self, text: str, size: float) -> tuple[str, float]:
        """SVG path data for `text` at `size` px with the baseline on y = 0, and its advance."""
        scale = size / self.upm
        pen = SVGPathPen(self.glyphs, ntos=lambda v: f"{v:.1f}".rstrip("0").rstrip("."))
        x = 0.0
        for ch in text:
            name = self.cmap.get(ord(ch)) or self.cmap.get(ord("?"))
            self.glyphs[name].draw(TransformPen(pen, (scale, 0, 0, -scale, x, 0)))
            x += self.hmtx[name][0] * scale
        return pen.getCommands(), x


def find_face(kind: str) -> Face:
    for d in FONT_DIRS:
        for name in FONT_CANDIDATES[kind]:
            p = os.path.join(d, name)
            if os.path.exists(p):
                return Face(p)
    sys.exit(f"no {kind} monospace font found in {FONT_DIRS}")


REG = find_face("regular")
BOLD = find_face("bold")


def esc(s: str) -> str:
    return html.escape(s, quote=True)


def text(x: float, y: float, s: str, size: float, fill: str = INK, face: Face = REG,
         anchor: str = "start") -> str:
    """Lettering as a path with its baseline at y; anchor start | middle | end."""
    d, adv = face.path(s, size)
    if anchor == "middle":
        x -= adv / 2
    elif anchor == "end":
        x -= adv
    return f'<path transform="translate({x:.1f},{y:.1f})" d="{d}" fill="{fill}"/>'


def advance(s: str, size: float, face: Face = REG) -> float:
    return face.path(s, size)[1]


def sheet(slug: str, w: int, h: int) -> str:
    return (f'<defs><pattern id="{slug}-g" width="24" height="24" patternUnits="userSpaceOnUse">'
            f'<path d="M24 0H0V24" fill="none" stroke="{MUTED}" stroke-opacity=".07"/></pattern></defs>'
            f'<rect width="{w}" height="{h}" rx="12" fill="{BG}"/>'
            f'<rect width="{w}" height="{h}" rx="12" fill="url(#{slug}-g)"/>'
            f'<rect x=".5" y=".5" width="{w - 1}" height="{h - 1}" rx="12" fill="none" stroke="{LINE}"/>')


def svg(slug: str, w: int, h: int, label: str, body: list[str]) -> str:
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" width="{w}" height="{h}" '
            f'role="img" aria-label="{esc(label)}">{sheet(slug, w, h)}{"".join(body)}</svg>\n')


def write(name: str, content: str) -> None:
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, name), "w", encoding="utf-8", newline="\n") as f:
        f.write(content)
    print(f"{name}: {len(content) // 1024} KB")


def card(x: float, y: float, w: float, h: float, fill: str = PANEL, stroke: str = LINE) -> str:
    return f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" rx="8" fill="{fill}" stroke="{stroke}"/>'


def head(x: float, y: float, d: str, color: str) -> str:
    """A small arrowhead at (x, y) pointing down ('v'), right ('h') or left ('l')."""
    if d == "v":
        pts = f"{x - 4:.1f},{y - 7:.1f} {x + 4:.1f},{y - 7:.1f} {x:.1f},{y:.1f}"
    elif d == "h":
        pts = f"{x - 7:.1f},{y - 4:.1f} {x - 7:.1f},{y + 4:.1f} {x:.1f},{y:.1f}"
    else:
        pts = f"{x + 7:.1f},{y - 4:.1f} {x + 7:.1f},{y + 4:.1f} {x:.1f},{y:.1f}"
    return f'<polygon points="{pts}" fill="{color}"/>'


def route(points: list[tuple[float, float]], color: str = TEAL, dash: bool = False) -> str:
    """An orthogonal connector through `points`, arrowhead at the last one."""
    d = "M" + " L".join(f"{x:.1f},{y:.1f}" for x, y in points)
    dash_attr = ' stroke-dasharray="5,4"' if dash else ""
    (px, py), (x, y) = points[-2], points[-1]
    direction = "v" if abs(x - px) < 0.5 else ("h" if x > px else "l")
    return (f'<path d="{d}" fill="none" stroke="{color}" stroke-width="1.4"{dash_attr}/>'
            + head(x, y, direction, color))


# ------------------------------------------------------------------ badges

BADGES = [
    # slug, label, value, accent (True: teal value segment)
    ("platform", "platform", "Windows · Linux", False),
    ("cpp", "C++", "20", True),
    ("cmake", "CMake", "3.28+", False),
    ("vulkan", "Vulkan", "1.3", True),
    ("license", "license", "MIT", False),
]


def badge(label: str, value: str, accent: bool = False, size: float = 11.5) -> str:
    """One pill badge in the page's skin: a muted label segment and a value segment, hairline border.
    Reusable as is: copy this function and the palette constants into another repository."""
    H, pad = 24, 10
    lw = advance(label, size) + pad * 2
    vw = advance(value, size, BOLD) + pad * 2
    w = lw + vw
    fill = TEAL if accent else PANEL2
    ink = BG if accent else INK
    body = [f'<clipPath id="b"><rect width="{w:.1f}" height="{H}" rx="12"/></clipPath>',
            f'<g clip-path="url(#b)"><rect width="{w:.1f}" height="{H}" fill="{PANEL}"/>'
            f'<rect x="{lw:.1f}" width="{vw:.1f}" height="{H}" fill="{fill}"/></g>',
            f'<rect x=".5" y=".5" width="{w - 1:.1f}" height="{H - 1}" rx="11.5" fill="none" stroke="{LINE}"/>',
            text(pad, 16.2, label, size, MUTED),
            text(lw + pad, 16.2, value, size, ink, BOLD)]
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w:.1f} {H}" width="{w:.1f}" height="{H}" '
            f'role="img" aria-label="{esc(label)}: {esc(value)}">{"".join(body)}</svg>\n')


# ------------------------------------------------------------------ banner

def banner() -> str:
    H = 320
    with open(os.path.join(OUT, "AppIcon.png"), "rb") as f:
        icon = "data:image/png;base64," + base64.b64encode(f.read()).decode()
    body = ['<defs><clipPath id="banner-icon"><rect x="64" y="60" width="200" height="200" rx="44"/></clipPath>'
            f'<linearGradient id="banner-wash" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="{DEEP}" stop-opacity=".4"/>'
            f'<stop offset=".55" stop-color="{BG}" stop-opacity="0"/></linearGradient></defs>',
            f'<rect x="1" y="1" width="{W - 2}" height="{H - 2}" rx="12" fill="url(#banner-wash)"/>',
            f'<image href="{icon}" x="64" y="60" width="200" height="200" clip-path="url(#banner-icon)"/>']
    x = 312
    body.append(text(x, 122, "OnyxSDK", 64, INK, BOLD))
    body.append(text(x, 164, "A C++20 SDK for game-asset tools.", 19, INK))
    body.append(text(x, 192, "A module writes the format. The SDK supplies the rest.", 19, MUTED))
    px = x
    for name in ["Core", "Gpu", "Render", "Ui", "Shell", "Media", "Cli", "Exchange", "TestKit"]:
        w = advance(name, 13) + 26
        body.append(f'<rect x="{px:.1f}" y="226" width="{w:.1f}" height="28" rx="14" fill="{PANEL}" stroke="{LINE}"/>')
        body.append(text(px + 13, 245, name, 13, INK))
        px += w + 8
    body.append(text(x, 288, "Nine libraries, each linking only the ones below it. Vulkan 1.3. Windows and Linux.", 13, MUTED))
    return svg("banner", W, H, "OnyxSDK: a C++20 SDK for game-asset tools, nine static libraries composed by your host", body)


# ------------------------------------------------------------------ section headers

HEADERS = {
    "shape": "The shape",
    "flow": "From a file to a view",
    "host": "What a host writes",
    "modules": "Modules",
    "build": "Build and verify",
    "where": "Where to look",
}


def header(title: str) -> str:
    H = 56
    body = [f'<rect width="{W}" height="{H}" rx="10" fill="{BG}"/>',
            f'<rect x=".5" y=".5" width="{W - 1}" height="{H - 1}" rx="10" fill="none" stroke="{LINE}"/>',
            text(32, 37, title, 22, INK, BOLD)]
    end = 32 + advance(title, 22, BOLD) + 24
    body.append(f'<line x1="{end:.1f}" y1="{H / 2}" x2="{W - 32}" y2="{H / 2}" stroke="{LINE}"/>')
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" '
            f'role="img" aria-label="{esc(title)}">{"".join(body)}</svg>\n')


# ------------------------------------------------------------------ the stack

LAYERS = {
    # name: (column, row, what it holds, third-party it carries)
    "Shell": (1, 4, "window · panels · commands · Run", "GLFW"),
    "Media": (2, 4, "source · audio output · view", "FFmpeg · miniaudio"),
    "Ui": (1, 3, "viewers · panels · theme", "ImGui"),
    "Render": (1, 2, "scene · camera · animation", "glslang"),
    "Gpu": (1, 1, "device · frames · pipelines", "volk · VMA"),
    "Cli": (0, 1, "probe · list · extract · decode", "no third party"),
    "Exchange": (2, 1, "glTF · PNG · WAV exporters", "cgltf · stb"),
    "TestKit": (3, 1, "goldens · fixtures · compare", "stb · toml++"),
    "Core": (1, 0, "vfs · types · domain · modules · workspace · jobs · settings · cli contract · log", "glm · toml++ · lz4"),
}


def stack() -> str:
    H = 720
    cw, ch, gx, gy = 268, 96, 22, 34
    left = (W - (4 * cw + 3 * gx)) / 2
    top = 48
    pos: dict[str, tuple[float, float, float]] = {}
    for name, (col, row, _, _) in LAYERS.items():
        x = left + col * (cw + gx)
        y = top + (4 - row) * (ch + gy)
        w = cw
        if name == "Core":
            x, w = left, 4 * cw + 3 * gx
        pos[name] = (x, y, w)

    def bottom(n):
        x, y, w = pos[n]
        return x + w / 2, y + ch

    def top_of(n):
        x, y, w = pos[n]
        return x + w / 2, y

    body = []
    for a, b in [("Shell", "Ui"), ("Ui", "Render"), ("Render", "Gpu"), ("Gpu", "Core")]:
        (x1, y1), (_, y2) = bottom(a), top_of(b)
        body.append(route([(x1, y1), (x1, y2)]))
    mx, my = bottom("Media")
    ux, uy, uw = pos["Ui"]
    mid = my + gy / 2
    body.append(route([(mx, my), (mx, mid), (ux + uw - 40, mid), (ux + uw - 40, uy)]))
    for n in ["Cli", "Exchange", "TestKit"]:
        x1, y1 = bottom(n)
        body.append(route([(x1, y1), (x1, pos["Core"][1])]))
    sx, sy, sw = pos["Shell"]
    cx_, cy, cw_ = pos["Cli"]
    body.append(route([(sx, sy + ch / 2), (cx_ + cw_ / 2, sy + ch / 2), (cx_ + cw_ / 2, cy)], MUTED, dash=True))
    for name, (col, row, sub, third) in LAYERS.items():
        x, y, w = pos[name]
        body.append(card(x, y, w, ch))
        body.append(text(x + 18, y + 32, f"Onyx::{name}", 18, INK, BOLD))
        body.append(text(x + 18, y + 56, sub, 12.5, MUTED))
        if third:
            body.append(text(x + 18, y + 78, third, 11.5, TEAL))
    ly = H - 30
    body.append(route([(left, ly), (left + 44, ly)]))
    body.append(text(left + 56, ly + 4, "links to, as part of its public interface", 12.5, MUTED))
    lx = left + 56 + advance("links to, as part of its public interface", 12.5) + 40
    body.append(route([(lx, ly), (lx + 44, ly)], MUTED, dash=True))
    body.append(text(lx + 56, ly + 4, "uses internally", 12.5, MUTED))
    body.append(text(left + 4 * cw + 3 * gx, ly + 4, "Your program links the layers it uses and nothing else.", 12.5, MUTED, anchor="end"))
    return svg("stack", W, H, "The nine layers: Core at the bottom; Gpu, Render, Ui and Shell stacked on it; Cli, Exchange and TestKit beside Gpu over Core; Media beside Shell over Ui", body)


# ------------------------------------------------------------------ from a file to a view

def flow() -> str:
    H = 400
    y0, h = 56, 212
    cols = [(40, 196), (264, 236), (528, 236), (792, 236)]
    cards = [("sample.prm", "a file", ["any container the", "module can read"]),
             ("IGameModule", "the module", ["Probe", "ParseContainer", "RegisterTypes", "RegisterDecoders", "RegisterSettings"]),
             ("Document", "the workspace", ["a tree of AssetEntry,", "each typed by TypeKey", "and MediaKind", "", "opened on the job queue"]),
             ("DecoderRegistry", "the registry", ["Scene · Image · Text", "Audio · Table · Raw", "Any · Schema", "", "decoded on demand"])]
    body = []
    for (x, w), (name, role, lines) in zip(cols, cards):
        body.append(card(x, y0, w, h))
        body.append(text(x + 18, y0 + 34, name, 16, INK, BOLD))
        body.append(text(x + 18, y0 + 56, role, 12.5, TEAL))
        yy = y0 + 92
        for ln in lines:
            if ln:
                body.append(text(x + 18, yy, ln, 12.5, MUTED))
            yy += 21
    for (x, w), (nx, _) in zip(cols, cols[1:]):
        body.append(route([(x + w, y0 + 40), (nx, y0 + 40)]))
    rx, rw = cols[3]
    ox, ow = 1060, 192
    outs = [("viewers", ["the ready-made app,", "or your own window"]), ("commands", ["probe · list · extract", "decode · render"]),
            ("exporters", ["glTF · PNG · WAV", "from the command line"])]
    sx, sy = rx + rw, y0 + 40
    jx = sx + 16
    r = 8
    for i, (name, lines) in enumerate(outs):
        yy = y0 + i * 76
        body.append(card(ox, yy, ow, 66, PANEL2, LINE))
        body.append(text(ox + 16, yy + 24, name, 14, INK, BOLD))
        body.append(text(ox + 16, yy + 42, lines[0], 11.5, MUTED))
        body.append(text(ox + 16, yy + 57, lines[1], 11.5, MUTED))
        ty = yy + 40
        if abs(ty - sy) < 1:
            d = f"M{sx},{sy} L{ox},{ty}"
        elif ty > sy:
            d = f"M{sx},{sy} L{jx - r},{sy} Q{jx},{sy} {jx},{sy + r} L{jx},{ty - r} Q{jx},{ty} {jx + r},{ty} L{ox},{ty}"
        else:
            d = f"M{sx},{sy} L{jx - r},{sy} Q{jx},{sy} {jx},{sy - r} L{jx},{ty + r} Q{jx},{ty} {jx + r},{ty} L{ox},{ty}"
        body.append(f'<path d="{d}" fill="none" stroke="{TEAL}" stroke-width="1.4"/>' + head(ox, ty, "h", TEAL))
    body.append(text(40, H - 48, "One decoded asset feeds a viewer, a command and an exporter alike; the module never knows which one asked.", 12.5, MUTED))
    body.append(text(40, H - 26, "Settings are declared by the module and edited in the app; diagnostics stay attached to the entry they describe.", 12.5, MUTED))
    return svg("flow", W, H, "From a file to a view: the module probes and parses, the workspace holds the document, the decoder registry decodes on demand, and viewers, CLI verbs and exporters consume the same payloads", body)


# ------------------------------------------------------------------ what a host writes

CODE = [
    ("Onyx::Shell::Shell shell({.appName = \"MinimalViewer\", .configDir = ...});", "the window, the panels, the viewers, the command line"),
    ("shell.Workspace().AddModule(std::make_unique<Prism::Module>());", "the format knowledge"),
    ("shell.AddExtension(std::make_unique<Prism::Ui::Extension>());", "one viewer of your own"),
    ("shell.SetAudioOutput(std::make_unique<Onyx::Media::AudioOutput>());", "sound, when Media is on"),
    ("Onyx::Media::RegisterMediaViewer(shell.Viewers());", "video, when Media is on"),
    ("return shell.Run(argc, argv);", "a file argument opens the window; anything else runs a command"),
]


def host() -> str:
    rows = 46
    H = 40 + len(CODE) * rows + 28 + 44
    body = [card(32, 32, W - 64, len(CODE) * rows + 28, PANEL)]
    for i, (line, note) in enumerate(CODE):
        yy = 32 + 14 + i * rows
        body.append(text(56, yy + 29, line, 13.5, INK))
        body.append(text(W - 56, yy + 29, note, 12, TEAL, anchor="end"))
        if i < len(CODE) - 1:
            body.append(f'<line x1="56" y1="{yy + rows:.1f}" x2="{W - 56}" y2="{yy + rows:.1f}" stroke="{LINE}" stroke-opacity=".7"/>')
    body.append(text(32, H - 22, "The complete program is Examples/MinimalViewer/Main.cpp: 29 lines, includes and error checks included.", 12.5, MUTED))
    return svg("host", W, H, "What a host writes: construct the Shell, add modules, add an extension, wire Media, call Run", body)


def main() -> None:
    for slug, label, value, accent in BADGES:
        write(f"badge-{slug}.svg", badge(label, value, accent))
    write("banner.svg", banner())
    for slug, title in HEADERS.items():
        write(f"h-{slug}.svg", header(title))
    write("stack.svg", stack())
    write("flow.svg", flow())
    write("host.svg", host())


if __name__ == "__main__":
    main()
