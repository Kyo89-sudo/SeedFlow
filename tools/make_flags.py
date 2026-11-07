"""Genera l'atlante delle bandierine per la scheda Peer.

Uso:  python tools/make_flags.py <cartella flag-icons/flags/4x3>

Le SVG provengono da https://github.com/lipis/flag-icons (licenza MIT, vedi
licenses/flag-icons.txt). Lo script le rasterizza con Playwright + Chromium in
una griglia di celle CELL_W x CELL_H e scrive:
  res/flags/flags.png   atlante PNG (righe di COLUMNS celle, ordine alfabetico)
  src/flag_codes.hpp    codici ISO 3166-1 alpha-2 nello stesso ordine
"""
import os
import pathlib
import sys

from playwright.sync_api import sync_playwright

CELL_W, CELL_H, COLUMNS = 40, 30, 16
ROOT = pathlib.Path(__file__).resolve().parent.parent


def main() -> None:
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    src = pathlib.Path(sys.argv[1]).resolve()
    codes = sorted(p.stem.upper() for p in src.glob("*.svg")
                   if len(p.stem) == 2 and p.stem.isalpha() and p.stem.lower() != "xx")
    if not codes:
        sys.exit("nessuna SVG a due lettere trovata in " + str(src))
    rows = (len(codes) + COLUMNS - 1) // COLUMNS
    width, height = COLUMNS * CELL_W, rows * CELL_H
    cells = "".join(
        f'<img src="{(src / (c.lower() + ".svg")).as_uri()}" '
        f'style="position:absolute;left:{(i % COLUMNS) * CELL_W}px;top:{(i // COLUMNS) * CELL_H}px;'
        f'width:{CELL_W}px;height:{CELL_H}px">'
        for i, c in enumerate(codes))
    page_file = ROOT / "build-flags.html"
    page_file.write_text(f"<!doctype html><body style='margin:0;background:transparent'>{cells}</body>",
                         encoding="utf-8")
    out = ROOT / "res" / "flags" / "flags.png"
    out.parent.mkdir(parents=True, exist_ok=True)
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch(executable_path=os.environ.get("CHROME_PATH") or None)
            page = browser.new_page(viewport={"width": width, "height": height}, device_scale_factor=1)
            page.goto(page_file.as_uri())
            page.wait_for_load_state("networkidle")
            page.screenshot(path=str(out), omit_background=True,
                            clip={"x": 0, "y": 0, "width": width, "height": height})
            browser.close()
    finally:
        page_file.unlink(missing_ok=True)

    header = ROOT / "src" / "flag_codes.hpp"
    lines = [
        "// Generato da tools/make_flags.py: non modificare a mano.",
        "// Codici ISO 3166-1 alpha-2 nell'ordine delle celle di res/flags/flags.png.",
        "#pragma once",
        "",
        "namespace sf::icons {",
        f"inline constexpr int kFlagCellW = {CELL_W};",
        f"inline constexpr int kFlagCellH = {CELL_H};",
        f"inline constexpr int kFlagColumns = {COLUMNS};",
        f"inline constexpr int kFlagCount = {len(codes)};",
        "inline constexpr char kFlagCodes[] =",
    ]
    for i in range(0, len(codes), 20):
        lines.append('    "' + "".join(codes[i:i + 20]) + '"')
    lines[-1] += ";"
    lines += ["} // namespace sf::icons", ""]
    header.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    print(f"{len(codes)} bandierine -> {out.relative_to(ROOT)} ({width}x{height})")


if __name__ == "__main__":
    main()
