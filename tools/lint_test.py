#!/usr/bin/env python3
"""Self-test for lint.py: one violation per rule, each reported exactly once."""
import sys
import tempfile
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import lint  # noqa: E402


def write(root, rel, text):
    p = root / rel
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(text, encoding="utf-8")


def main():
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        clean = "#pragma once\n\nint Value();\n"
        write(root, "Source/Core/include/Onyx/Core/Clean.h", clean)
        # (a) forbidden include
        write(root, "Source/Core/include/Onyx/Core/Inc.h", "#pragma once\n#include <imgui.h>\n")
        # (b) singleton accessor
        write(root, "Source/Core/Single.cpp", "struct S {};\nstatic S& Instance() { static S s; return s; }\n")
        # (c) game name
        write(root, "Examples/Demo/main.cpp", "// opens a wad archive\nint main() { return 0; }\n")
        # (d) missing pragma once
        write(root, "Source/Core/include/Onyx/Core/NoGuard.h", "int Value();\n")
        # (e) history comment
        write(root, "Source/Core/Hist.cpp", "// Task 4 added this\nint Value() { return 1; }\n")
        # (f) style colors written outside the theme
        write(root, "Source/Ui/Widget.cpp", "void F() { ImGui::GetStyle().Colors[0] = {}; }\n")
        write(root, "Source/Ui/Widget2.cpp", "void H() { style.Colors[1] = {}; }\n")
        write(root, "Source/Shell/Bar.cpp", "void I() { ImGui::GetStyle().Colors[2] = {}; }\n")
        write(root, "Source/Ui/Theme.cpp", "void G() { ImGui::GetStyle().Colors[0] = {}; }\n")
        # skipped trees and non-violations
        write(root, "legacy/Old.cpp", "#include <imgui.h>\n// wad Task 1\n")
        write(root, "Tests/Golden/data.txt", "wad\n")
        write(root, "Source/Core/Fine.cpp", "#include <windows.h_not>\nint Swadd = 0; // wadding\n")
        found = lint.lint(root)

    rules = Counter(line.split(": ")[1] for line in found)
    expected = {
        "forbidden-include": 1,
        "singleton-accessor": 1,
        "game-name": 1,
        "pragma-once": 1,
        "comment-history": 1,
        "style-writer": 3,
    }
    if dict(rules) != expected:
        print("unexpected result:", dict(rules))
        for line in found:
            print("  " + line)
        return 1
    print("ok 6")
    return 0


if __name__ == "__main__":
    sys.exit(main())
