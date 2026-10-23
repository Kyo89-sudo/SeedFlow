# SeedFlow icon set "B": SF Symbols-inspired duotone (coloured rounded line + light tint).
# 24-unit grid, 1.7 stroke, round caps/joins. Original SeedFlow drawings, no external icon pack.
# Colours match the progress bars and the speed graph (paint.hpp).
from pathlib import Path

PAL = {'blue': '#1A73C8', 'green': '#2E9B45', 'orange': '#EF7D00', 'red': '#D93025', 'gray': '#5F6B78',
       'amber': '#E0A000', 'teal': '#00838F', 'purple': '#7B3FB8', 'navy': '#2B4A6F'}

def tint(c, a=.16):
    r, g, b = (int(c[i:i + 2], 16) for i in (1, 3, 5))
    return '#%02x%02x%02x' % tuple(round(v + (255 - v) * (1 - a)) for v in (r, g, b))

CIR = 'M12 3a9 9 0 1 1 0 18a9 9 0 1 1 0-18z'
FOLDER = 'M3.5 7a1.5 1.5 0 0 1 1.5-1.5h4.2l2 2.2H19a1.5 1.5 0 0 1 1.5 1.5v8.8a1.5 1.5 0 0 1-1.5 1.5H5a1.5 1.5 0 0 1-1.5-1.5z'
DOC = 'M6 3.5h7.5l4.5 4.5v12.5H6z'
CARD = 'M5 5h14a1.5 1.5 0 0 1 1.5 1.5v11A1.5 1.5 0 0 1 19 19H5a1.5 1.5 0 0 1-1.5-1.5v-11A1.5 1.5 0 0 1 5 5z'
GEAR = ('M10.4 3h3.2l.5 2.3 1.8.8 2-1.3 2.3 2.3-1.3 2 .8 1.8 2.3.5v3.2l-2.3.5-.8 1.8 1.3 2-2.3 2.3-2-1.3-1.8.8'
        '-.5 2.3h-3.2l-.5-2.3-1.8-.8-2 1.3-2.3-2.3 1.3-2-.8-1.8L3 13.6v-3.2l2.3-.5.8-1.8-1.3-2 2.3-2.3 2 1.3 1.8-.8z')

# name: (colour, [(path, role)]) - role s = line, f = tinted shape with outline, k = solid
I = {
    'add':         ('blue',   [(DOC, 'f'), ('M13.5 3.5V8H18', 's'), ('M12 11v6M9 14h6', 's')]),
    'magnet':      ('red',    [('M5 4h4.5v8a2.5 2.5 0 0 0 5 0V4H19v8a7 7 0 0 1-14 0z', 'f'), ('M5 8h4.5M14.5 8H19', 's')]),
    'play':        ('green',  [(CIR, 'f'), ('M10 8.3v7.4l5.8-3.7z', 'k')]),
    'pause':       ('orange', [(CIR, 'f'), ('M10 8.5v7M14 8.5v7', 's')]),
    'remove':      ('gray',   [(CIR, 'f'), ('M9 9l6 6M15 9l-6 6', 's')]),
    'delete':      ('red',    [('M6 6.5l1 13.5h10l1-13.5', 'f'), ('M4 6.5h16M9 6.5V4.5h6v2M10 10v6.5M14 10v6.5', 's')]),
    'up':          ('blue',   [('M12 19.5V5M6.5 10.5L12 5l5.5 5.5', 's')]),
    'down':        ('blue',   [('M12 4.5V19M6.5 13.5L12 19l5.5-5.5', 's')]),
    'superseed':   ('amber',  [('M13 2.5L5 13.5h6l-1 8 8-11h-6z', 'f')]),
    'ratio':       ('teal',   [('M12 4v16M8 20h8M5 7h14', 's'), ('M2.5 14l2.5-7 2.5 7a2.5 2.2 0 0 1-5 0zM16.5 14l2.5-7 2.5 7a2.5 2.2 0 0 1-5 0z', 'f')]),
    'wizard':      ('purple', [('M4 20L15 9', 's'), ('M15 9l2-2 2 2-2 2z', 'f'), ('M8 3v3M6.5 4.5h3M19 13v3M17.5 14.5h3M13 3v2M12 4h2', 's')]),
    'stats':       ('blue',   [('M5.5 13h3v7.5h-3zM10.5 6h3v14.5h-3zM15.5 10h3v10.5h-3z', 'f'), ('M4 20.5h16', 's')]),
    'diagnostics': ('green',  [(CIR, 'f'), ('M5.5 12.5h3l1.7-3.8 3.1 7.3 1.7-3.5h3.5', 's')]),
    'refresh':     ('blue',   [('M19.5 11A7.5 7.5 0 0 0 6 7.2M4.5 13A7.5 7.5 0 0 0 18 16.8M5.5 3.5v4h4M18.5 20.5v-4h-4', 's')]),
    'copy':        ('gray',   [('M15.5 7.5v-3a1 1 0 0 0-1-1h-10a1 1 0 0 0-1 1v11a1 1 0 0 0 1 1h3', 's'),
                               ('M8.5 8.5h11a1 1 0 0 1 1 1v10a1 1 0 0 1-1 1h-11a1 1 0 0 1-1-1v-10a1 1 0 0 1 1-1z', 'f')]),
    'folder':      ('amber',  [(FOLDER, 'f'), ('M3.5 10h17', 's')]),
    'globe':       ('blue',   [(CIR, 'f'), ('M12 3c-2.5 2.7-3.7 5.7-3.7 9s1.2 6.3 3.7 9M12 3c2.5 2.7 3.7 5.7 3.7 9s-1.2 6.3-3.7 9M3.5 9h17M3.5 15h17', 's')]),
    'settings':    ('gray',   [(GEAR, 'f'), ('M12 9a3 3 0 1 1 0 6a3 3 0 1 1 0-6z', 's')]),
    'info':        ('blue',   [(CIR, 'f'), ('M12 11v5.5M12 7.8v.2', 's')]),
    'download':    ('blue',   [(CIR, 'f'), ('M12 7.5v9M8.5 13l3.5 3.5 3.5-3.5', 's')]),
    'upload':      ('green',  [(CIR, 'f'), ('M12 16.5v-9M8.5 11L12 7.5l3.5 3.5', 's')]),
    'health':      ('red',    [('M12 20s-8-4.6-8-10.2A4.3 4.3 0 0 1 12 7.3a4.3 4.3 0 0 1 8 2.5C20 15.4 12 20 12 20z', 'f'),
                               ('M6.5 12h2.5l1.5-2.5 2.5 5 1.5-2.5h3', 's')]),
    'peers':       ('teal',   [('M9 4a3.2 3.2 0 1 1 0 6.4A3.2 3.2 0 1 1 9 4zM3 20v-1a6 6 0 0 1 12 0v1z', 'f'),
                               ('M16 4.5a3 3 0 0 1 0 5.6M18 14a5 5 0 0 1 3 4.5V20', 's')]),
    'file':        ('gray',   [(DOC, 'f'), ('M13.5 3.5V8H18M9 12.5h6M9 16h4', 's')]),
    'error':       ('red',    [('M12 3.5l9 16H3z', 'f'), ('M12 9.5v4.5M12 17v.2', 's')]),
    'check':       ('green',  [(CIR, 'f'), ('M8 12.3l2.8 2.8 5.2-5.4', 's')]),
    'queue':       ('navy',   [(CIR, 'f'), ('M12 7v5l3.5 2', 's')]),
    'tracker':     ('blue',   [('M12 9.7a2.3 2.3 0 1 1 0 4.6a2.3 2.3 0 1 1 0-4.6z', 'f'),
                               ('M8 8a5.7 5.7 0 0 0 0 8M16 8a5.7 5.7 0 0 1 0 8M4.8 4.8a10.2 10.2 0 0 0 0 14.4M19.2 4.8a10.2 10.2 0 0 1 0 14.4', 's')]),
    'log':         ('gray',   [('M6 3.5h12a1.5 1.5 0 0 1 1.5 1.5v14a1.5 1.5 0 0 1-1.5 1.5H6A1.5 1.5 0 0 1 4.5 19V5A1.5 1.5 0 0 1 6 3.5z', 'f'),
                               ('M8 8h8M8 12h8M8 16h5', 's')]),
    'all':         ('navy',   [('M12 3.5l8.5 4.5-8.5 4.5L3.5 8z', 'f'), ('M3.5 12.5l8.5 4.5 8.5-4.5M3.5 16.5l8.5 4.5 8.5-4.5', 's')]),
    'checking':    ('orange', [('M10.5 4a6.5 6.5 0 1 1 0 13a6.5 6.5 0 1 1 0-13z', 'f'), ('M15.3 15.3l5 5M7.8 10.6l1.8 1.8 3.4-3.6', 's')]),
    'moving':      ('purple', [(FOLDER, 'f'), ('M8 13.5h8M13.3 10.8l2.7 2.7-2.7 2.7', 's')]),
    'identity':    ('gray',   [(CARD, 'f'), ('M8.5 9a2 2 0 1 1 0 4a2 2 0 1 1 0-4zM5.8 16.5a2.8 2.8 0 0 1 5.4 0M14 10h4M14 14h3', 's')]),
    'preferences': ('gray',   [('M4 7h9M17 7h3M4 17h3M11 17h9', 's'),
                               ('M15 4.5a2.5 2.5 0 1 1 0 5a2.5 2.5 0 1 1 0-5zM9 14.5a2.5 2.5 0 1 1 0 5a2.5 2.5 0 1 1 0-5z', 'f')]),
    'exit':        ('red',    [('M13 4h5.5A1.5 1.5 0 0 1 20 5.5v13a1.5 1.5 0 0 1-1.5 1.5H13', 's'), ('M15 12H4M8 8l-4 4 4 4', 's')]),
    'speed':       ('teal',   [('M3 17.5a9 9 0 1 1 18 0z', 'f'), ('M12 16.5l4.3-5M5.5 12.5l1.2.7M12 6v1.5M18.5 12.5l-1.2.7', 's')]),
    'calendar':    ('navy',   [(CARD, 'f'), ('M3.5 9.5h17M8 3v4M16 3v4M7.5 13h2M11 13h2M14.5 13h2M7.5 16h2M11 16h2', 's')]),
    'turtle':      ('teal',   [('M4 16.5a7.5 7 0 0 1 15 0z', 'f'),
                               ('M17.5 13.5l1.5-1.5h1.5a1.5 1.5 0 0 1 1.5 1.5v.5a1.5 1.5 0 0 1-1.5 1.5H18M6.5 16.5l-1 2.5M16 16.5l1 2.5M3 16.5h16.5M9 16.5l1.5-3.5h2l1.5 3.5', 's')]),
    'window':      ('navy',   [('M5 4.5h14A1.5 1.5 0 0 1 20.5 6v12a1.5 1.5 0 0 1-1.5 1.5H5A1.5 1.5 0 0 1 3.5 18V6A1.5 1.5 0 0 1 5 4.5z', 'f'),
                               ('M3.5 9h17M6.5 6.8h.2M9 6.8h.2', 's')]),
    'cpu':         ('purple', [('M7 6h10a1 1 0 0 1 1 1v10a1 1 0 0 1-1 1H7a1 1 0 0 1-1-1V7a1 1 0 0 1 1-1z', 'f'),
                               ('M9.5 9.5h5v5h-5zM9 3v3M15 3v3M9 18v3M15 18v3M3 9h3M3 15h3M18 9h3M18 15h3', 's')]),
    'bandwidth':   ('teal',   [('M3.5 18l5-6 4 3 4.5-7 3.5 4v6.5h-17z', 'f'), ('M3.5 20.5h17', 's')]),
    'priority':    ('amber',  [('M12 3.2l2.7 5.6 6.1.8-4.5 4.2 1.1 6.1L12 17l-5.4 2.9 1.1-6.1-4.5-4.2 6.1-.8z', 'f')]),
    'create':      ('teal',   [('M19 13v5.5a1.5 1.5 0 0 1-1.5 1.5h-12A1.5 1.5 0 0 1 4 18.5v-12A1.5 1.5 0 0 1 5.5 5H11', 's'),
                               ('M17.5 3.5l3 3L12 15l-3.8.8L9 12z', 'f')]),
}

def svg(colour, parts):
    c = PAL[colour]
    body = ''
    for d, role in parts:
        if role == 's':
            body += f'<path d="{d}" fill="none" stroke="{c}"/>'
        elif role == 'f':
            body += f'<path d="{d}" fill="{tint(c)}" stroke="{c}"/>'
        else:
            body += f'<path d="{d}" fill="{c}" stroke="{c}"/>'
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke-width="1.7" '
            'stroke-linecap="round" stroke-linejoin="round">' + body + '</svg>')

def generate():
    out = Path(__file__).resolve().parent / 'svg'
    out.mkdir(parents=True, exist_ok=True)
    for name, (colour, parts) in I.items():
        (out / (name + '.svg')).write_text(svg(colour, parts), encoding='utf-8')
    print(len(I), 'original SeedFlow icons')

if __name__ in ('__main__', 'icons_def'):
    generate()
