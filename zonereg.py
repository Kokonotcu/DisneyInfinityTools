"""Register a 1.0 playset in the 3.0 zone list (plan PS-1): the 1.0 zone record verbatim, the 1.0 PlaysetProperties
record plus the keys 3.0 needs, and an OrderedPlaysets entry. Only 3.0-side registration data changes; the 1.0
files are not touched.

SUPERSEDED by di1compat's zones10 (gameplay::playsets10): every 1.0 playset is registered as the zone list loads, with
the overlay below derived by rule and ids assigned automatically; no file is written. This script stays as that
code's reference (its Incredibles records come out byte for byte the same) and to --revert a zone list it wrote.

The 1.0 zone list is compiled Lua (startup2/gamedb/core/zonelist.lua), so its records are taken from the running
chunk (lupa, luabc.to64) and written out as Lua text -- tables that mix an array part and named keys (BeaconMap
rows) survive, which a JSON detour would not guarantee.

Overlay on PlaysetProperties.Incredibles (each needed by 3.0, measured by the playset research, _research\\playset.md):
  PlaysetUniqueID 3 -> 11 (3 is Empire in 3.0; 3.0 uses 1-10)   WorldIsPlayset (3.0 key; 1.0 spelled worldIsPlayset on
  the zone)   SaveGameKey / SaveGamePrefix   LoadingScreens (3.0 key; 1.0 LoadingScreen)   the three menu flags.

python zonereg.py [--playset Incredibles --zone IN_City] [--zonelist <3.0 zonelist.lua>] [--dry] [--revert]
"""
import argparse, shutil, sys
from pathlib import Path
import paths
from luabc import to64

IDENT = __import__('re').compile(r'^[A-Za-z_][A-Za-z0-9_]*$')
OVERLAY = {'Incredibles': {'PlaysetUniqueID': 11, 'WorldIsPlayset': True, 'SaveGameKey': 'Incredibles',
                           'SaveGamePrefix': 'IN', 'LoadingScreens': 'IN/LoadingScreen_Inc', '_ShowInMenu_': True,
                           'availableInConsumerBuild': True, 'availableInShippingBuild': True}}


def load10(path):
    """The 1.0 zone list's Records as live Lua tables (lupa)."""
    import lupa.lua51 as L
    lua = L.LuaRuntime(unpack_returned_tuples=True)
    f = lua.eval('loadstring')(to64(Path(path).read_bytes()))
    if isinstance(f, tuple):
        raise SystemExit('1.0 zonelist does not load: %s' % (f[1],))
    f()
    return lua, lua.globals().ZoneList.Records


def lua_text(lua, value, indent):
    """3.0-style Lua text of a value: the array part first, then named keys sorted."""
    t = lua.eval('type')(value)
    if t == 'string':
        return '"%s"' % value.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n')
    if t == 'boolean':
        return 'true' if value else 'false'
    if t == 'number':
        return str(int(value)) if float(value).is_integer() else repr(float(value))
    if t != 'table':
        raise ValueError('cannot write a %s' % t)
    n = int(lua.eval('function(t) return #t end')(value))
    parts = [lua_text(lua, value[i], indent + 1) for i in range(1, n + 1)]
    keys = [k for k in value.keys() if not (lua.eval('type')(k) == 'number' and 1 <= k <= n and float(k).is_integer())]
    for k in sorted(keys, key=lambda k: (lua.eval('type')(k), str(k))):
        name = k if isinstance(k, str) and IDENT.match(k) else '[%s]' % lua_text(lua, k, 0)
        parts.append('%s = %s' % (name, lua_text(lua, value[k], indent + 1)))
    return '{ ' + ''.join(p + ', ' for p in parts) + '}'


def record_text(lua, name, rec, extra=None):
    lines = ['\t\t\t%s = ' % name, '\t\t\t{']
    keys = sorted(set(rec.keys()) | set(extra or ()), key=str)
    for k in keys:
        v = (extra or {}).get(k)
        if k in (extra or {}):
            text = 'true' if v is True else 'false' if v is False else ('"%s"' % v if isinstance(v, str) else str(v))
        else:
            text = lua_text(lua, rec[k], 4)
        lines.append('\t\t\t\t%s = %s,' % (k if IDENT.match(k) else '["%s"]' % k, text))
    lines.append('\t\t\t},')
    return lines


def insert_after_open(lines, header):
    """Index just after the '{' that opens the group whose header line is `header`."""
    i = lines.index(header)
    assert lines[i + 1].strip() == '{', lines[i + 1]
    return i + 2


def main():
    ap = argparse.ArgumentParser(description='Register a 1.0 playset in the 3.0 zone list')
    ap.add_argument('--playset', default='Incredibles')
    ap.add_argument('--zone', default='IN_City')
    ap.add_argument('--zonelist', help='3.0 zonelist.lua (default: the install loose copy)')
    ap.add_argument('--zonelist10', help='1.0 compiled zonelist (default: <ext10>/assets/startup2/gamedb/core/zonelist.lua)')
    ap.add_argument('--dry', action='store_true')
    ap.add_argument('--revert', action='store_true')
    paths.add_args(ap)
    a = paths.use(ap.parse_args())
    z30 = Path(a.zonelist) if a.zonelist else paths.game('assets', 'gamedb', 'core', 'zonelist.lua')
    orig = z30.with_name(z30.name + '.pre-' + a.playset.lower())
    if a.revert:
        shutil.copyfile(orig, z30)
        print('restored', z30, 'from', orig)
        return
    text = z30.read_bytes().decode('utf-8')
    lines = text.split('\n')
    if any(l.rstrip() == '\t\t\t%s = ' % a.zone.rstrip() or l.rstrip() == '\t\t\t%s =' % a.zone for l in lines):
        sys.exit('%s is already registered in %s (use --revert first)' % (a.zone, z30))
    lua, R = load10(a.zonelist10 or paths.ext10('startup2', 'gamedb', 'core', 'zonelist.lua'))
    zone, pp = R.Zones[a.zone], R.PlaysetProperties[a.playset]
    if zone is None or pp is None:
        sys.exit('1.0 zone list has no %s / %s' % (a.zone, a.playset))
    zone_lines = record_text(lua, a.zone, zone)
    pp_lines = record_text(lua, a.playset, pp, OVERLAY.get(a.playset))
    ordered = '\t\t\t\t{ Name = "%s", availableInConsumerBuild = true, availableInShippingBuild = true,\t\t\t\t},' % a.playset
    # bottom-up, so earlier indices stay valid
    for header, new in (('\t\tZones = ', zone_lines), ('\t\tPlaysetProperties = ', pp_lines),
                        ('\t\t\tOrderedPlaysets = ', [ordered])):
        hdr = next(l for l in lines if l.rstrip() == header.rstrip())
        at = insert_after_open(lines, hdr)
        lines[at:at] = new
    out = '\n'.join(lines)
    print('\n'.join(zone_lines[:3] + ['\t\t\t\t...'] + pp_lines[:3]))
    print('+%d lines (%s, %s, OrderedPlaysets)' % (len(zone_lines) + len(pp_lines) + 1, a.zone, a.playset))
    if a.dry:
        return
    if not orig.exists():
        shutil.copyfile(z30, orig)
    z30.write_bytes(out.encode('utf-8'))
    print('wrote', z30, '(original kept as %s)' % orig.name)


if __name__ == '__main__':
    main()
