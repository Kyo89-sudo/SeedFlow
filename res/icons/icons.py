# Original SeedFlow Classic artwork. 24-unit grid; no external icon pack.
from pathlib import Path
C = {'green': '#288E49', 'blue': '#2176BB', 'orange': '#D88115', 'red': '#CC413B', 'gray': '#586575', 'amber': '#D79B14', 'teal': '#14868C', 'purple': '#8052B2', 'dgreen': '#287944', 'navy': '#3E5973'}

def svg(color, body, fill=True):
    if body.startswith('<svg'):
        return body
    rgb = tuple(int(color[i:i+2], 16) for i in (1, 3, 5))
    light = '#' + ''.join(f'{round(c + (255-c)*.38):02x}' for c in rgb)
    edge = '#' + ''.join(f'{round(c*.78):02x}' for c in rgb)
    body = body.replace('FILL', 'fill="url(#tone)" stroke="none"')
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" '
            f'stroke="{edge}" stroke-width="1.3" stroke-linecap="round" stroke-linejoin="round">'
            f'<defs><linearGradient id="tone" x1="0" y1="0" x2="0" y2="1">'
            f'<stop stop-color="{light}"/><stop offset="1" stop-color="{color}"/>'
            f'</linearGradient></defs>' + body + '</svg>')

I = {
    'add': ('green', '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><defs>\n <linearGradient id="green" x2="0" y2="1"><stop stop-color="#81BF53"/><stop offset="1" stop-color="#388B35"/></linearGradient>\n <linearGradient id="blue" x2="0" y2="1"><stop stop-color="#6BAEDC"/><stop offset="1" stop-color="#2D76B1"/></linearGradient>\n <linearGradient id="amber" x2="0" y2="1"><stop stop-color="#F4CC64"/><stop offset="1" stop-color="#D39224"/></linearGradient>\n <linearGradient id="red" x2="0" y2="1"><stop stop-color="#EC8A7D"/><stop offset="1" stop-color="#BE4940"/></linearGradient>\n <linearGradient id="paper" x2="0" y2="1"><stop stop-color="#FFFFFF"/><stop offset="1" stop-color="#DCE5ED"/></linearGradient>\n <linearGradient id="metal" x2="0" y2="1"><stop stop-color="#CBD5DD"/><stop offset="1" stop-color="#7B8D9E"/></linearGradient>\n </defs><g stroke-linejoin="round" stroke-linecap="round"><path d="M4 2.5h10l4 4V21H4z" fill="url(#paper)" stroke="#667C8D"/><path d="M14 2.5v4h4" fill="#CAD8E3" stroke="#667C8D"/><path d="M7 9h7M7 12h5M7 15h3" stroke="#A0B0BD"/><circle cx="16.5" cy="16.5" r="6" fill="url(#green)" stroke="#39732D"/><path d="M16.5 13v7M13 16.5h7" stroke="white" stroke-width="1.8"/></g></svg>'),
    'magnet': ('blue', '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><defs>\n <linearGradient id="green" x2="0" y2="1"><stop stop-color="#81BF53"/><stop offset="1" stop-color="#388B35"/></linearGradient>\n <linearGradient id="blue" x2="0" y2="1"><stop stop-color="#6BAEDC"/><stop offset="1" stop-color="#2D76B1"/></linearGradient>\n <linearGradient id="amber" x2="0" y2="1"><stop stop-color="#F4CC64"/><stop offset="1" stop-color="#D39224"/></linearGradient>\n <linearGradient id="red" x2="0" y2="1"><stop stop-color="#EC8A7D"/><stop offset="1" stop-color="#BE4940"/></linearGradient>\n <linearGradient id="paper" x2="0" y2="1"><stop stop-color="#FFFFFF"/><stop offset="1" stop-color="#DCE5ED"/></linearGradient>\n <linearGradient id="metal" x2="0" y2="1"><stop stop-color="#CBD5DD"/><stop offset="1" stop-color="#7B8D9E"/></linearGradient>\n </defs><g stroke-linejoin="round" stroke-linecap="round"><path d="M4 3h5v10a3 3 0 0 0 6 0V3h5v10a8 8 0 0 1-16 0z" fill="url(#blue)" stroke="#346D98"/><path d="M4 3h5v5H4zM15 3h5v5h-5z" fill="url(#paper)" stroke="#346D98"/><path d="M6 10v3a6 6 0 0 0 12 0v-3" fill="none" stroke="#B9DCF2" stroke-width=".8"/></g></svg>'),
    'play': ('green', '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><defs>\n <linearGradient id="green" x2="0" y2="1"><stop stop-color="#81BF53"/><stop offset="1" stop-color="#388B35"/></linearGradient>\n <linearGradient id="blue" x2="0" y2="1"><stop stop-color="#6BAEDC"/><stop offset="1" stop-color="#2D76B1"/></linearGradient>\n <linearGradient id="amber" x2="0" y2="1"><stop stop-color="#F4CC64"/><stop offset="1" stop-color="#D39224"/></linearGradient>\n <linearGradient id="red" x2="0" y2="1"><stop stop-color="#EC8A7D"/><stop offset="1" stop-color="#BE4940"/></linearGradient>\n <linearGradient id="paper" x2="0" y2="1"><stop stop-color="#FFFFFF"/><stop offset="1" stop-color="#DCE5ED"/></linearGradient>\n <linearGradient id="metal" x2="0" y2="1"><stop stop-color="#CBD5DD"/><stop offset="1" stop-color="#7B8D9E"/></linearGradient>\n </defs><g stroke-linejoin="round" stroke-linecap="round"><circle cx="12" cy="12" r="9.5" fill="url(#green)" stroke="#447D35"/><path d="M4.5 11a7.6 7.6 0 0 1 15-1" fill="none" stroke="#B9DC99" stroke-width=".8"/><path d="M10 7.5l7 4.5-7 4.5z" fill="#FFFFFF" stroke="#E4F2D9" stroke-width=".6"/></g></svg>'),
    'pause': ('orange', '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><defs>\n <linearGradient id="green" x2="0" y2="1"><stop stop-color="#81BF53"/><stop offset="1" stop-color="#388B35"/></linearGradient>\n <linearGradient id="blue" x2="0" y2="1"><stop stop-color="#6BAEDC"/><stop offset="1" stop-color="#2D76B1"/></linearGradient>\n <linearGradient id="amber" x2="0" y2="1"><stop stop-color="#F4CC64"/><stop offset="1" stop-color="#D39224"/></linearGradient>\n <linearGradient id="red" x2="0" y2="1"><stop stop-color="#EC8A7D"/><stop offset="1" stop-color="#BE4940"/></linearGradient>\n <linearGradient id="paper" x2="0" y2="1"><stop stop-color="#FFFFFF"/><stop offset="1" stop-color="#DCE5ED"/></linearGradient>\n <linearGradient id="metal" x2="0" y2="1"><stop stop-color="#CBD5DD"/><stop offset="1" stop-color="#7B8D9E"/></linearGradient>\n </defs><g stroke-linejoin="round" stroke-linecap="round"><rect x="3" y="3" width="18" height="18" rx="3" fill="url(#amber)" stroke="#AC7C28"/><path d="M5 9V6a1 1 0 0 1 1-1h12" fill="none" stroke="#FFE5A6" stroke-width=".8"/><path d="M8 7h3v10H8zM14 7h3v10h-3z" fill="#FFFCF2" stroke="#E2B75B" stroke-width=".5"/></g></svg>'),
    'remove': ('gray', '<circle FILL cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="9"/><path d="M8.5 8.5l7 7M15.5 8.5l-7 7"/>'),
    'delete': ('red', '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><defs>\n <linearGradient id="green" x2="0" y2="1"><stop stop-color="#81BF53"/><stop offset="1" stop-color="#388B35"/></linearGradient>\n <linearGradient id="blue" x2="0" y2="1"><stop stop-color="#6BAEDC"/><stop offset="1" stop-color="#2D76B1"/></linearGradient>\n <linearGradient id="amber" x2="0" y2="1"><stop stop-color="#F4CC64"/><stop offset="1" stop-color="#D39224"/></linearGradient>\n <linearGradient id="red" x2="0" y2="1"><stop stop-color="#EC8A7D"/><stop offset="1" stop-color="#BE4940"/></linearGradient>\n <linearGradient id="paper" x2="0" y2="1"><stop stop-color="#FFFFFF"/><stop offset="1" stop-color="#DCE5ED"/></linearGradient>\n <linearGradient id="metal" x2="0" y2="1"><stop stop-color="#CBD5DD"/><stop offset="1" stop-color="#7B8D9E"/></linearGradient>\n </defs><g stroke-linejoin="round" stroke-linecap="round"><path d="M6 7h12l-1 14H7z" fill="url(#red)" stroke="#A14D47"/><path d="M4 6h16v2H4z" fill="url(#paper)" stroke="#8E6B68"/><path d="M9 6V3h6v3" fill="none" stroke="#A14D47" stroke-width="1.5"/><path d="M9 10l.5 8M12 10v8M15 10l-.5 8" stroke="#F8C5BB" stroke-width="1"/></g></svg>'),
    'up': ('navy', '<path d="M12 20V4M6 10l6-6 6 6"/>'),
    'down': ('navy', '<path d="M12 4v16M6 14l6 6 6-6"/>'),
    'superseed': ('amber', '<path FILL d="M13 2L4 14h7l-1 8 10-13h-8z"/><path d="M13 2L4 14h7l-1 8 10-13h-8z"/>'),
    'ratio': ('teal', '<path d="M12 3v18M8 21h8M4 7h16M5 7l-3 8h6zM19 7l-3 8h6z"/><path FILL d="M2 15h6a3 3 0 0 1-6 0M16 15h6a3 3 0 0 1-6 0"/><path d="M2 15a3 3 0 0 0 6 0M16 15a3 3 0 0 0 6 0"/>'),
    'wizard': ('purple', '<path FILL d="M4 18L16 6l3 3L7 21z"/><path d="M4 18L16 6l3 3L7 21zM13 9l3 3M7 3v4M5 5h4M20 2v3M18.5 3.5h3M20 15v4M18 17h4"/>'),
    'stats': ('blue', '<path d="M3 21h18"/><path FILL d="M5 12h3v6H5zM10.5 5h3v13h-3zM16 9h3v9h-3z"/><path d="M5 12h3v6H5zM10.5 5h3v13h-3zM16 9h3v9h-3z"/>'),
    'diagnostics': ('green', '<circle FILL cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="9"/><path d="M5 12h3l2-4 3 8 2-4h4"/>'),
    'refresh': ('blue', '<path d="M20 10a8 8 0 0 0-14-4L3 9M3 4v5h5M4 14a8 8 0 0 0 14 4l3-3M16 15h5v5"/>'),
    'copy': ('gray', '<rect FILL x="8" y="8" width="12" height="13" rx="1.5"/><rect x="8" y="8" width="12" height="13" rx="1.5"/><path d="M15 8V3H3v13h5"/>'),
    'folder': ('amber', '<path FILL d="M3 5h6l2 3h10l-2 12H3z"/><path d="M3 20V5h6l2 3h9v3M3 20l3-9h15l-2 9z"/>'),
    'globe': ('blue', '<circle FILL cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="9"/><ellipse cx="12" cy="12" rx="4" ry="9"/><path d="M3 12h18M5 6.5h14M5 17.5h14"/>'),
    'settings': ('gray', '<path FILL d="M9 3h6l1 3 3 1 2 5-2 5-3 1-1 3H9l-1-3-3-1-2-5 2-5 3-1z"/><path d="M9 3h6l1 3 3 1 2 5-2 5-3 1-1 3H9l-1-3-3-1-2-5 2-5 3-1z"/><circle cx="12" cy="12" r="3.5"/>'),
    'info': ('navy', '<circle FILL cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="9"/><path d="M11 11h1v6M10 17h4M12 7v.2"/>'),
    'download': ('blue', '<path FILL d="M3 16h18v4H3z"/><path d="M3 16v4h18v-4M12 3v12M7 10l5 5 5-5"/>'),
    'upload': ('green', '<path FILL d="M3 16h18v4H3z"/><path d="M3 16v4h18v-4M12 15V3M7 8l5-5 5 5"/>'),
    'health': ('red', '<path FILL d="M12 20L4 12C-1 5 7 1 12 7c5-6 13-2 8 5z"/><path d="M12 20L4 12C-1 5 7 1 12 7c5-6 13-2 8 5zM4 12h4l2-3 3 6 2-3h5"/>'),
    'peers': ('dgreen', '<circle FILL cx="9" cy="8" r="3"/><circle cx="9" cy="8" r="3"/><path d="M3 20v-2a6 6 0 0 1 12 0v2M16 5a3 3 0 0 1 0 6M18 14a5 5 0 0 1 3 4v2"/>'),
    'file': ('gray', '<path FILL d="M5 3h9l5 5v13H5z"/><path d="M5 3h9l5 5v13H5zM14 3v5h5M8 12h8M8 16h6"/>'),
    'error': ('red', '<circle FILL cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="9"/><path d="M12 7v6M12 17v.2"/>'),
    'check': ('green', '<circle FILL cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="9"/><path d="M7.5 12l3 3 6-6"/>'),
    'queue': ('gray', '<circle FILL cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="9"/><path d="M12 6v6l4 2"/>'),
    'tracker': ('blue', '<circle FILL cx="12" cy="12" r="2"/><circle cx="12" cy="12" r="2"/><path d="M8 8a5.6 5.6 0 0 0 0 8M16 8a5.6 5.6 0 0 1 0 8M5 5a10 10 0 0 0 0 14M19 5a10 10 0 0 1 0 14"/>'),
    'log': ('gray', '<rect FILL x="4" y="3" width="16" height="18" rx="1.5"/><rect x="4" y="3" width="16" height="18" rx="1.5"/><path d="M8 8h8M8 12h8M8 16h5"/>'),
    'all': ('navy', '<path FILL d="M12 3l9 5-9 5-9-5z"/><path d="M12 3l9 5-9 5-9-5zM3 13l9 5 9-5M3 17l9 5 9-5"/>'),
    'checking': ('orange', '<circle FILL cx="10" cy="10" r="6.5"/><circle cx="10" cy="10" r="6.5"/><path d="M15 15l6 6M7 10l2 2 4-4"/>'),
    'moving': ('purple', '<path FILL d="M3 5h6l2 3h10v12H3z"/><path d="M3 5h6l2 3h10v12H3zM8 14h9M14 11l3 3-3 3"/>'),
    'identity': ('gray', '<rect FILL x="3" y="5" width="18" height="15" rx="1.5"/><rect x="3" y="5" width="18" height="15" rx="1.5"/><circle cx="8" cy="11" r="2"/><path d="M5 17a3 3 0 0 1 6 0M14 10h4M14 14h4"/>'),
    'preferences': ('navy', '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><defs>\n <linearGradient id="green" x2="0" y2="1"><stop stop-color="#81BF53"/><stop offset="1" stop-color="#388B35"/></linearGradient>\n <linearGradient id="blue" x2="0" y2="1"><stop stop-color="#6BAEDC"/><stop offset="1" stop-color="#2D76B1"/></linearGradient>\n <linearGradient id="amber" x2="0" y2="1"><stop stop-color="#F4CC64"/><stop offset="1" stop-color="#D39224"/></linearGradient>\n <linearGradient id="red" x2="0" y2="1"><stop stop-color="#EC8A7D"/><stop offset="1" stop-color="#BE4940"/></linearGradient>\n <linearGradient id="paper" x2="0" y2="1"><stop stop-color="#FFFFFF"/><stop offset="1" stop-color="#DCE5ED"/></linearGradient>\n <linearGradient id="metal" x2="0" y2="1"><stop stop-color="#CBD5DD"/><stop offset="1" stop-color="#7B8D9E"/></linearGradient>\n </defs><g stroke-linejoin="round" stroke-linecap="round"><path d="M10 2h4l.7 3 2 .9 2.7-1.4 2 3.5-2 2.1.1 2.6 2 2.1-2 3.5-2.8-.9-2 .9L14 22h-4l-.7-3.2-2-.9-2.8.9-2-3.5 2-2.1.1-2.6-2-2.1 2-3.5L7.3 6l2-.9z" fill="url(#metal)" stroke="#5C7082" stroke-width=".9"/><circle cx="12" cy="12" r="5.1" fill="#F2F5F7" stroke="#6C8191"/><circle cx="12" cy="12" r="3.4" fill="url(#blue)" stroke="#3C739A"/></g></svg>'),
    'exit': ('gray', '<path d="M13 4h7v16h-7M15 12H3M7 8l-4 4 4 4"/>'),
    'speed': ('teal', '<path FILL d="M3 18a9 9 0 0 1 18 0z"/><path d="M3 18a9 9 0 0 1 18 0M3 18h18M12 17l5-7M5 11l2 1M12 6v2M19 11l-1 1"/>'),
    'calendar': ('navy', '<rect FILL x="3" y="5" width="18" height="16" rx="1.5"/><rect x="3" y="5" width="18" height="16" rx="1.5"/><path d="M3 10h18M8 3v4M16 3v4M7 14h2M11 14h2M15 14h2M7 17h2M11 17h2"/>'),
    'turtle': ('teal', '<path FILL d="M5 15a7 6 0 0 1 14 0z"/><path d="M5 15a7 6 0 0 1 14 0zM19 13h2v3h-3M7 15l-1 3M17 15l1 3M3 12l2 3M9 11l3-2 3 2"/>'),
    'window': ('navy', '<rect FILL x="3" y="4" width="18" height="16" rx="1.5"/><rect x="3" y="4" width="18" height="16" rx="1.5"/><path d="M3 9h18M6 6.5h1M10 6.5h1"/>'),
    'cpu': ('purple', '<rect FILL x="6" y="6" width="12" height="12" rx="1"/><rect x="6" y="6" width="12" height="12" rx="1"/><path d="M9 9h6v6H9zM9 3v3M15 3v3M9 18v3M15 18v3M3 9h3M3 15h3M18 9h3M18 15h3"/>'),
    'bandwidth': ('teal', '<path FILL d="M3 18l5-6 4 3 4-7 5 5v8H3z"/><path d="M3 18l5-6 4 3 4-7 5 5M3 21h18"/>'),
    'priority': ('orange', '<path FILL d="M12 3l3 6 6 1-4.5 4.5 1 6-5.5-3-5.5 3 1-6L3 10l6-1z"/><path d="M12 3l3 6 6 1-4.5 4.5 1 6-5.5-3-5.5 3 1-6L3 10l6-1z"/>'),
    'create': ('green', '<path FILL d="M5 3h8l5 5v5l-7 8H5z"/><path d="M11 21H5V3h8l5 5v4M13 3v5h5M8 12h5M8 16h2M13 21l1-4 6-6 3 3-6 6zM18 13l3 3"/>'),
}

def generate():
    out = Path(__file__).resolve().parent / 'svg'
    out.mkdir(parents=True, exist_ok=True)
    for name, (color, body, *rest) in I.items():
        (out / (name + '.svg')).write_text(svg(C[color], body), encoding='utf-8')
    print(len(I), 'original SeedFlow icons')

if __name__ in ('__main__', 'icons_def'):
    generate()
