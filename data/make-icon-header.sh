#!/bin/sh
# Renders the PNG icons from neo-postoffice.svg and regenerates
# include/window_icon.h (the X11 window icon). Needs rsvg-convert and python3.
set -e
cd "$(dirname "$0")"
for s in 16 22 24 32 48 64 128 256; do
	rsvg-convert -w $s -h $s neo-postoffice.svg -o icons/neo-postoffice-$s.png
done
python3 - <<'PY'
sizes = [16, 32, 48, 64, 128]
out = ['// Generated from data/neo-postoffice.svg (data/icons/*.png): the window icon.',
       '// Regenerate with data/make-icon-header.sh.', '#ifndef _WINDOW_ICON_H', '#define _WINDOW_ICON_H 1', '']
for s in sizes:
    b = open(f'icons/neo-postoffice-{s}.png', 'rb').read()
    out.append(f'static const unsigned char window_icon_{s}[{len(b)}] = {{')
    for i in range(0, len(b), 16):
        out.append('\t' + ', '.join(f'0x{x:02x}' for x in b[i:i+16]) + ',')
    out.append('};')
out.append('static const struct { const unsigned char *data; int size; int px; } window_icons[] = {')
for s in sizes:
    out.append(f'\t{{ window_icon_{s}, (int) sizeof (window_icon_{s}), {s} }},')
out += ['};', '', '#endif', '']
open('../include/window_icon.h', 'w').write('\n'.join(out))
PY
