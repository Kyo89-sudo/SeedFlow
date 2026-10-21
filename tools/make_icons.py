"""SeedFlow - rasterizza il set di icone (stile A, res/icons/svg) in strisce PNG
per ogni dimensione e genera src/icon_ids.hpp. Serve solo se si modificano le icone:
i PNG generati sono gia' inclusi nei sorgenti.
Requisiti: Python 3 + Playwright con Chromium (pip install playwright; playwright install chromium)."""
import pathlib, runpy, base64
root = pathlib.Path(__file__).resolve().parent.parent
icons = root / 'res' / 'icons'
ns = runpy.run_path(str(icons / 'icons.py'), run_name='icons_def')  # rigenera svg/
names = list(ns['I'].keys())
SIZES = [16, 20, 24, 28, 32, 40, 48]
from playwright.sync_api import sync_playwright
with sync_playwright() as p:
    br = p.chromium.launch()
    for s in SIZES:
        imgs = ''.join(
            f'<img width="{s}" height="{s}" src="data:image/svg+xml;base64,'
            f'{base64.b64encode((icons/"svg"/(n+".svg")).read_bytes()).decode()}">' for n in names)
        pg = br.new_page(viewport={'width': s * len(names), 'height': s}, device_scale_factor=1)
        pg.set_content(f'<html><body style="margin:0;background:transparent;display:flex;line-height:0">{imgs}</body></html>')
        pg.wait_for_timeout(200)
        pg.screenshot(path=str(icons / f'strip{s}.png'), omit_background=True,
                      clip={'x': 0, 'y': 0, 'width': s * len(names), 'height': s})
        pg.close()
    br.close()
ids = ',\n'.join(f'    Ic{"".join(w.capitalize() for w in n.split("_"))} = {i}' for i, n in enumerate(names))
(root / 'src' / 'icon_ids.hpp').write_text(
    '// Generato da tools/make_icons.py - non modificare a mano.\n#pragma once\n\nnamespace sf::icons {\n'
    f'enum Id : int {{\n{ids},\n    IcCount = {len(names)}\n}};\n'
    f'inline constexpr int kStripSizes[] = {{{", ".join(map(str, SIZES))}}};\n}} // namespace sf::icons\n')
# res/SeedFlow.rc contiene una riga "<2000+N> RCDATA icons/stripN.png" per ogni dimensione.
print(len(names), 'icone,', SIZES)
