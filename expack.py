"""Merge expendable packages into one 3.0 package file.

3.0 loads packages per PLAYSET: assets/expendables_data/<playset>.lua (pentane.log once
expendables_data.zip is disabled: playsetx.lua, toybox.lua). 1.0 packages are named by type
(TerrainBuildingsExpendables). Each is a Lua 5.1 chunk (32-bit BIG-endian bytecode) setting the global
ExpendablesPackage = {Expendables = {...}, Groups = {...}}; the loader (FUN_0160aa30) reads only that.

A text wrapper that loadstring()s the embedded bytecode did NOT run in-game (0 of 34 playsetx-only
definitions registered), so this writes the merged table as a plain Lua literal instead.

python expack.py <out.lua> <base package> <extra package> [...]
"""
from pathlib import Path
from luabc import run_package


def lit(v, ind=''):
    if isinstance(v, bool):
        return 'true' if v else 'false'
    if isinstance(v, (int, float)):
        return repr(v)
    if isinstance(v, str):
        return '"' + ''.join(c if 32 <= ord(c) < 127 and c not in '"\\' else '\\%d' % ord(c) for c in v) + '"'
    if isinstance(v, dict):
        keys = list(v)
        if keys == list(range(1, len(keys) + 1)):    # array part
            items = [lit(v[k], ind + ' ') for k in keys]
        else:
            items = ['[%s] = %s' % (lit(k), lit(v[k], ind + ' ')) for k in keys]
        return '{\n' + ''.join('%s %s,\n' % (ind, x) for x in items) + ind + '}'
    raise TypeError(type(v))


# 1.0 entries have no pop-in range; 3.0 then stores 0 (definition +0x9c/+0xa0 = squared distances) and the
# expendable never pops in, so its model is never even requested. (1000, 1200) is a shipped 3.0 pair (75x).
PORT_DEFAULTS = {'PopinDistance': 1000, 'PopoutDistance': 1200}


def merged(pkgs):
    base = pkgs[0]
    for p in pkgs[1:]:
        for key in ('Expendables', 'Groups'):
            dst = base.setdefault(key, {})
            for v in (p.get(key) or {}).values():
                if key == 'Expendables':
                    v = dict(PORT_DEFAULTS, **v)
                dst[len(dst) + 1] = v
    return 'ExpendablesPackage = ' + lit(base) + '\n'


if __name__ == '__main__':
    import argparse
    ap = argparse.ArgumentParser(description='Merge expendable packages into one plain-Lua 3.0 package')
    ap.add_argument('out', help='.lua to write')
    ap.add_argument('base', help='package whose entries come first (the 3.0 playset package)')
    ap.add_argument('extra', nargs='+', help='packages appended to it (1.0 packages get pop-in defaults)')
    a = ap.parse_args()
    pkgs = [run_package(p) for p in [a.base] + a.extra]
    Path(a.out).write_text(merged(pkgs), encoding='ascii')
    print('wrote %s: %d expendables, %d groups' % (a.out, len(pkgs[0]['Expendables']), len(pkgs[0]['Groups'])))
