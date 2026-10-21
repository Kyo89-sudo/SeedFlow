# SeedFlow icon set: original 24x24 duotone icons (stroke 2, round joins).
C = dict(green='#2E9B45', blue='#1A73C8', orange='#EF7D00', red='#D93025', gray='#5F6B78',
         amber='#F2A900', teal='#00838F', purple='#7B3FB8', dgreen='#1E7B34', navy='#2B4A6F')
def svg(color, body, fill=True):
    f = color if fill else 'none'
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="{color}" '
            f'stroke-width="2" stroke-linecap="round" stroke-linejoin="round">'
            + body.replace('FILL', f'fill="{color}" fill-opacity="0.18" stroke="none"') + '</svg>')
I = {
 'add': ('green', '<path FILL d="M6 3h8l5 5v13H6z"/><path d="M6 3h8l5 5v13H6z"/><path d="M14 3v5h5"/><path d="M12.5 11v7M9 14.5h7"/>'),
 'magnet': ('blue', '<path FILL d="M5 4h4v8a3 3 0 0 0 6 0V4h4v8a7 7 0 0 1-14 0z"/><path d="M5 4h4v8a3 3 0 0 0 6 0V4h4v8a7 7 0 0 1-14 0z"/><path d="M5 8h4M15 8h4"/>'),
 'play': ('green', '<path FILL d="M7 4.5v15a1 1 0 0 0 1.5.86l12-7.5a1 1 0 0 0 0-1.72l-12-7.5A1 1 0 0 0 7 4.5z"/><path d="M7 4.5v15a1 1 0 0 0 1.5.86l12-7.5a1 1 0 0 0 0-1.72l-12-7.5A1 1 0 0 0 7 4.5z"/>'),
 'pause': ('orange', '<rect FILL x="6" y="4" width="4" height="16" rx="1"/><rect FILL x="14" y="4" width="4" height="16" rx="1"/><rect x="6" y="4" width="4" height="16" rx="1"/><rect x="14" y="4" width="4" height="16" rx="1"/>'),
 'remove': ('gray', '<circle FILL cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="9"/><path d="M9 9l6 6M15 9l-6 6"/>'),
 'delete': ('red', '<path FILL d="M6 7h12l-1 13a1 1 0 0 1-1 1H8a1 1 0 0 1-1-1z"/><path d="M4 7h16M10 3h4a1 1 0 0 1 1 1v3H9V4a1 1 0 0 1 1-1z"/><path d="M6 7l1 13a1 1 0 0 0 1 1h8a1 1 0 0 0 1-1l1-13"/><path d="M10 11v6M14 11v6"/>'),
 'up': ('navy', '<path d="M12 19V5M6 11l6-6 6 6"/>', False),
 'down': ('navy', '<path d="M12 5v14M6 13l6 6 6-6"/>', False),
 'superseed': ('amber', '<path FILL d="M13 2L4 14h7l-1 8 9-12h-7z"/><path d="M13 2L4 14h7l-1 8 9-12h-7z"/>'),
 'ratio': ('teal', '<path d="M12 3v18M7 21h10M5 7h14"/><path FILL d="M2 15l3-8 3 8a3 3 0 0 1-6 0zM16 15l3-8 3 8a3 3 0 0 1-6 0z"/><path d="M2 15l3-8 3 8a3 3 0 0 1-6 0zM16 15l3-8 3 8a3 3 0 0 1-6 0z"/>'),
 'wizard': ('purple', '<path d="M4 20L15 9"/><path FILL d="M15 9l2-2 2 2-2 2z"/><path d="M13.5 7.5l2 2"/><path d="M18 2v3M16.5 3.5h3M21 8v2M20 9h2M9 3v2M8 4h2"/>'),
 'stats': ('blue', '<path d="M3 21h18"/><rect FILL x="5" y="11" width="3" height="7"/><rect FILL x="10.5" y="5" width="3" height="13"/><rect FILL x="16" y="8" width="3" height="10"/><rect x="5" y="11" width="3" height="7" rx=".5"/><rect x="10.5" y="5" width="3" height="13" rx=".5"/><rect x="16" y="8" width="3" height="10" rx=".5"/>'),
 'diagnostics': ('green', '<circle FILL cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="9"/><path d="M5 12h3l2-4 3 8 2-4h4"/>'),
 'refresh': ('blue', '<path d="M20 11a8 8 0 0 0-14.6-4.5M4 13a8 8 0 0 0 14.6 4.5"/><path d="M5 3v4h4M19 21v-4h-4"/>', False),
 'copy': ('gray', '<rect FILL x="8" y="8" width="13" height="13" rx="2"/><rect x="8" y="8" width="13" height="13" rx="2"/><path d="M16 8V5a2 2 0 0 0-2-2H5a2 2 0 0 0-2 2v9a2 2 0 0 0 2 2h3"/>'),
 'folder': ('amber', '<path FILL d="M3 6a1 1 0 0 1 1-1h5l2 2h9a1 1 0 0 1 1 1v11a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1z"/><path d="M3 6a1 1 0 0 1 1-1h5l2 2h9a1 1 0 0 1 1 1v11a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1z"/><path d="M3 10h18"/>'),
 'globe': ('blue', '<circle FILL cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="9"/><path d="M3 12h18M12 3a14 14 0 0 1 0 18M12 3a14 14 0 0 0 0 18"/>'),
 'settings': ('gray', '<circle FILL cx="12" cy="12" r="3.5"/><circle cx="12" cy="12" r="3.5"/><path d="M12 2.5l1.6 2.6 3-.7.7 3 2.6 1.6-1.4 2.7 1.4 2.7-2.6 1.6-.7 3-3-.7L12 21.5l-1.6-2.6-3 .7-.7-3-2.6-1.6L5.5 12 4.1 9.3l2.6-1.6.7-3 3 .7z"/>'),
 'info': ('navy', '<circle FILL cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="9"/><path d="M12 11v6M12 7.5v.01"/>'),
 'download': ('blue', '<path FILL d="M4 15v4a1 1 0 0 0 1 1h14a1 1 0 0 0 1-1v-4z"/><path d="M4 15v4a1 1 0 0 0 1 1h14a1 1 0 0 0 1-1v-4"/><path d="M12 3v12M7 10l5 5 5-5"/>'),
 'upload': ('green', '<path FILL d="M4 15v4a1 1 0 0 0 1 1h14a1 1 0 0 0 1-1v-4z"/><path d="M4 15v4a1 1 0 0 0 1 1h14a1 1 0 0 0 1-1v-4"/><path d="M12 15V3M7 8l5-5 5 5"/>'),
 'health': ('red', '<path FILL d="M12 20s-8-4.6-8-10.5A4.5 4.5 0 0 1 12 7a4.5 4.5 0 0 1 8 2.5C20 15.4 12 20 12 20z"/><path d="M12 20s-8-4.6-8-10.5A4.5 4.5 0 0 1 12 7a4.5 4.5 0 0 1 8 2.5C20 15.4 12 20 12 20z"/><path d="M3 12h4l1.5-3 3 6 1.5-3h8"/>'),
 'peers': ('dgreen', '<circle FILL cx="9" cy="8" r="3.5"/><circle cx="9" cy="8" r="3.5"/><path d="M2.5 20a6.5 6.5 0 0 1 13 0"/><path d="M16 4.6a3.5 3.5 0 0 1 0 6.8M18 14a6.5 6.5 0 0 1 3.5 6"/>'),
 'file': ('gray', '<path FILL d="M6 3h8l5 5v13H6z"/><path d="M6 3h8l5 5v13H6z"/><path d="M14 3v5h5M9 13h7M9 17h5"/>'),
 'error': ('red', '<circle FILL cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="9"/><path d="M12 7v6M12 16.5v.01"/>'),
 'check': ('green', '<circle FILL cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="9"/><path d="M8 12.5l3 3 5-6"/>'),
 'queue': ('gray', '<circle FILL cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/>'),
 'tracker': ('blue', '<circle FILL cx="12" cy="12" r="2"/><circle cx="12" cy="12" r="2"/><path d="M8.5 15.5a5 5 0 0 1 0-7M15.5 8.5a5 5 0 0 1 0 7M5.6 18.4a9 9 0 0 1 0-12.8M18.4 5.6a9 9 0 0 1 0 12.8"/>'),
 'log': ('gray', '<rect FILL x="4" y="3" width="16" height="18" rx="2"/><rect x="4" y="3" width="16" height="18" rx="2"/><path d="M8 8h8M8 12h8M8 16h5"/>'),
 'all': ('navy', '<path FILL d="M12 3l9 5-9 5-9-5z"/><path d="M12 3l9 5-9 5-9-5z"/><path d="M3 13l9 5 9-5M3 17l9 5 9-5" opacity=".6"/>'),
 'checking': ('orange', '<circle FILL cx="10.5" cy="10.5" r="6.5"/><circle cx="10.5" cy="10.5" r="6.5"/><path d="M15.5 15.5L21 21M8 10.5l2 2 3.5-4"/>'),
 'moving': ('purple', '<path FILL d="M3 6a1 1 0 0 1 1-1h5l2 2h9a1 1 0 0 1 1 1v11a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1z"/><path d="M3 6a1 1 0 0 1 1-1h5l2 2h9a1 1 0 0 1 1 1v11a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1z"/><path d="M9 14h7M13 11l3 3-3 3"/>'),
 'identity': ('gray', '<rect FILL x="3" y="5" width="18" height="14" rx="2"/><rect x="3" y="5" width="18" height="14" rx="2"/><circle cx="9" cy="11" r="2"/><path d="M6 16a3 3 0 0 1 6 0M15 10h3M15 14h3"/>'),
 'preferences': ('navy', '<path d="M4 6h8M16 6h4M4 12h3M11 12h9M4 18h11M19 18h1"/><circle FILL cx="14" cy="6" r="2"/><circle FILL cx="9" cy="12" r="2"/><circle FILL cx="17" cy="18" r="2"/><circle cx="14" cy="6" r="2"/><circle cx="9" cy="12" r="2"/><circle cx="17" cy="18" r="2"/>'),
 'exit': ('gray', '<path d="M14 4h4a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2h-4"/><path d="M9 8l-4 4 4 4M5 12h11"/>', False),
 'speed': ('teal', '<path FILL d="M4 17a8 8 0 1 1 16 0z"/><path d="M4 17a8 8 0 1 1 16 0"/><path d="M12 17l4-5"/><path d="M3 17h18"/>'),
 'calendar': ('navy', '<rect FILL x="3" y="5" width="18" height="16" rx="2"/><rect x="3" y="5" width="18" height="16" rx="2"/><path d="M3 10h18M8 3v4M16 3v4M7 14h2M11 14h2M15 14h2M7 17h2M11 17h2"/>'),
 'turtle': ('teal', '<path FILL d="M5 15a7 6 0 0 1 14 0z"/><path d="M5 15a7 6 0 0 1 14 0z"/><path d="M19 15h2a1 1 0 0 0 0-2h-1.5M7 15l-1 3M17 15l1 3M3 13l2 2M9 11l3-2 3 2"/>'),
 'window': ('navy', '<rect FILL x="3" y="4" width="18" height="16" rx="2"/><rect x="3" y="4" width="18" height="16" rx="2"/><path d="M3 9h18M6 6.5h.01M8.5 6.5h.01"/>'),
 'cpu': ('purple', '<rect FILL x="6" y="6" width="12" height="12" rx="1.5"/><rect x="6" y="6" width="12" height="12" rx="1.5"/><rect x="9.5" y="9.5" width="5" height="5" rx=".5"/><path d="M9 2v4M15 2v4M9 18v4M15 18v4M2 9h4M2 15h4M18 9h4M18 15h4"/>'),
 'bandwidth': ('teal', '<path FILL d="M3 18l5-6 4 3 4-6 5 5v6H3z"/><path d="M3 18l5-6 4 3 4-6 5 5"/><path d="M3 21h18"/>'),
 'priority': ('orange', '<path FILL d="M12 3l2.6 5.6 6.1.7-4.5 4.2 1.2 6L12 16.6 6.6 19.5l1.2-6-4.5-4.2 6.1-.7z"/><path d="M12 3l2.6 5.6 6.1.7-4.5 4.2 1.2 6L12 16.6 6.6 19.5l1.2-6-4.5-4.2 6.1-.7z"/>'),
 'create': ('green', '<path FILL d="M5 3h8l5 5v6.5L12.5 20H5z"/><path d="M12 21H6a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1h7l5 5v4"/><path d="M13 3v5h5"/><path d="M15 21l1-3.5 4.5-4.5a1.4 1.4 0 0 1 2 2L18 19.5z"/><path d="M8 12h5M8 16h3"/>'),
}
import os
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'svg')
os.makedirs(out, exist_ok=True)
for k,(c,b,*rest) in I.items():
    open(os.path.join(out, f'{k}.svg'),'w').write(svg(C[c], b, rest[0] if rest else True))
print(len(I))
