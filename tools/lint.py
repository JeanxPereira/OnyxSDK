#!/usr/bin/env python3
"""Layer-hygiene lint for the Onyx tree.

Usage: lint.py [root]

Prints `path:line: rule: detail` per violation and exits 1 if any were found.
"""
import re
import sys
from pathlib import Path

SKIP_DIRS = {"legacy", "third_party", "build", ".superpowers", ".git"}

_CORE = ["imgui", "volk", "vk_mem_alloc", "vulkan/", "GLFW/", "windows.h", "libav", "miniaudio"]
FORBIDDEN_INCLUDES = {
    "Core": _CORE,
    "Cli": _CORE,
    "Exchange": _CORE,
    "TestKit": _CORE,
    "Gpu": ["imgui", "GLFW/", "libav", "miniaudio"],
    "Render": ["imgui", "GLFW/", "libav", "miniaudio"],
    "Ui": ["GLFW/", "libav", "miniaudio"],
    "Shell": ["libav"],
    "Media": ["GLFW/"],
}

GAME_NAMES = re.compile(r"\b(gow|gowr|gow1|gow2|ragnarok|wad|vag|vpk|pss)\b", re.IGNORECASE)
SINGLETON = re.compile(r"static\s+\w+&\s+(Get|Instance)\s*\(")
INCLUDE_LINE = re.compile(r"^\s*#\s*include\b")
PRAGMA_ONCE = re.compile(r"^\s*#\s*pragma\s+once\b", re.MULTILINE)
HISTORY_CASE = re.compile(r"Task \d|\bT\d{1,2}:|\bM\d ")
HISTORY_ICASE = re.compile(r"fix-round|milestone", re.IGNORECASE)

STYLE_WRITE = re.compile(r"GetStyle\(\)\.Colors\[|\bstyle\.Colors\[")
STYLE_WRITER_FILE = "Source/Ui/Theme.cpp"
STYLE_LAYERS = {"Ui", "Shell"}

HEADER_EXT = {".h", ".hpp", ".hh", ".inl"}
SOURCE_EXT = {".cpp", ".cc", ".cxx", ".c"}


def read_text(path):
    data = path.read_bytes()
    if b"\0" in data:
        return None
    return data.decode("utf-8", errors="replace")


def walk(base, skip_golden=False):
    if not base.is_dir():
        return
    for p in sorted(base.rglob("*")):
        if not p.is_file():
            continue
        rel = p.relative_to(base).parts
        if any(part in SKIP_DIRS for part in rel):
            continue
        if skip_golden and rel and rel[0] == "Golden":
            continue
        yield p


def comment_lines(text):
    """Yield (lineno, comment_text) for the comment portions of each line."""
    in_block = False
    for n, line in enumerate(text.splitlines(), 1):
        out = []
        i = 0
        while i < len(line):
            if in_block:
                end = line.find("*/", i)
                if end < 0:
                    out.append(line[i:])
                    i = len(line)
                else:
                    out.append(line[i:end])
                    i = end + 2
                    in_block = False
            else:
                a = line.find("//", i)
                b = line.find("/*", i)
                if a < 0 and b < 0:
                    break
                if b < 0 or (0 <= a < b):
                    out.append(line[a + 2:])
                    break
                in_block = True
                i = b + 2
        if out:
            yield n, " ".join(out)


def lint(root):
    root = Path(root)
    found = []

    def report(path, line, rule, detail):
        found.append(f"{path.relative_to(root).as_posix()}:{line}: {rule}: {detail}")

    source = root / "Source"

    # (a) forbidden includes, (b) singleton accessors, (d) pragma once, (e) history comments
    if source.is_dir():
        for layer_dir in sorted(p for p in source.iterdir() if p.is_dir()):
            tokens = FORBIDDEN_INCLUDES.get(layer_dir.name, [])
            include_dir = layer_dir / "include"
            for p in walk(layer_dir):
                ext = p.suffix.lower()
                is_public = include_dir in p.parents
                if ext not in HEADER_EXT and ext not in SOURCE_EXT:
                    continue
                text = read_text(p)
                if text is None:
                    continue
                active = [t for t in tokens if is_public or t != "windows.h"]
                for n, line in enumerate(text.splitlines(), 1):
                    if not INCLUDE_LINE.match(line):
                        continue
                    low = line.lower()
                    for t in active:
                        if t.lower() in low:
                            report(p, n, "forbidden-include",
                                   f"{layer_dir.name} may not include '{t}'")
                for m in SINGLETON.finditer(text):
                    n = text.count("\n", 0, m.start()) + 1
                    report(p, n, "singleton-accessor", m.group(0).strip())
                if is_public and ext in HEADER_EXT and not PRAGMA_ONCE.search(text):
                    report(p, 1, "pragma-once", "public header lacks #pragma once")
                for n, c in comment_lines(text):
                    m = HISTORY_CASE.search(c) or HISTORY_ICASE.search(c)
                    if m:
                        report(p, n, "comment-history", f"'{m.group(0).strip()}' in comment")

    # (f) style colors are written only by the theme
    if source.is_dir():
        for layer in sorted(STYLE_LAYERS):
            for p in walk(source / layer):
                if p.suffix.lower() not in HEADER_EXT | SOURCE_EXT:
                    continue
                if p.relative_to(root).as_posix() == STYLE_WRITER_FILE:
                    continue
                text = read_text(p)
                if text is None:
                    continue
                for n, line in enumerate(text.splitlines(), 1):
                    m = STYLE_WRITE.search(line)
                    if m:
                        report(p, n, "style-writer", f"'{m.group(0)}' outside Source/Ui/Theme.cpp")

    # (c) game names
    for sub, skip_golden in (("Source", False), ("Examples", False), ("Tests", True)):
        for p in walk(root / sub, skip_golden):
            text = read_text(p)
            if text is None:
                continue
            for n, line in enumerate(text.splitlines(), 1):
                m = GAME_NAMES.search(line)
                if m:
                    report(p, n, "game-name", f"'{m.group(0)}'")

    return found


def main(argv):
    root = Path(argv[1]) if len(argv) > 1 else Path(__file__).resolve().parent.parent
    found = lint(root)
    for f in found:
        print(f)
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
