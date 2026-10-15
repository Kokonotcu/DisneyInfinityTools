"""1.0 -> 3.0 port numbering of the ConnectionTemplates both games own (motion-clip output ids, model links).

A motion clip's output id is a port of its LayerBind's ConnectionTemplate (3.0 FUN_00532050: id -> the model's
PortToLocalIndices -> DataNode; 1.0 FUN_005444c0: id -> DataNodeLinks). The template file itself holds no names
(.bct: Filters / Mirrors only); a port's connection is named by the models: BaseDataNodeLinks maps port -> local ->
ConnectionMapUsedDataNodes -> DataNode, whose header path without the model's root ('Anchor/Cntrl_Elbow_Right/
Transform' -> 'cntrl_elbow_right/transform', case-folded) is the name. Per game and template a port is named by the
majority over every model linking it (a name kept only on its best-voted port). to30[p] = the 3.0 port of 1.0 port
p's name (or of the one 3.0 name it equals below an extra root level, 'anchor/cntrl_reardoor/transform'); a 1.0 name
3.0 has no port for gets a distinct spare 3.0 port (one no 3.0 model links, so 3.0 models skip it; a 1.0 model
renumbered by the same table keeps that joint); a port no 1.0 model links shares one more spare. Without a spare left
the port is dropped (DROP, table()).
Evidence (twin clips, the ids never used to build the table): rule.py in wt/_scratch/anim10, octtest --clip-ids.

usage: portmap.py scan <assets> <out.json> [sub ...]   models' port names (default subs: characters choreographies)
       portmap.py header <links10.json> <links30.json> <out.hpp>
Run scans under capped.ps1."""
import sys, os, json, collections, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# template -> 3.0 port count (Filter length of the 3.0 .bct, else the longest shipped PortToLocalIndices; both agree
# where both exist). Every template 3.0 owns that 1.0 motion clips name, except the ones whose check failed (none on
# twins) or could not be made: characters/pir_kraken/pir_kraken.ct (no 3.0 twin clip or model; 44 of its 51 1.0 names
# have no 3.0 port, the 3.0 rig has 18). Per-template evidence: wt/_scratch/general (census, clipids_all, modelcheck).
TEMPLATES = {'characters/biped/biped.ct': 968, 'characters/_townspeople/_townspeople.ct': 70,
             'characters/omnidroid/omnidroid.ct': 54,
             'characters/car/car.ct': 188, 'characters/ts_bullseye/ts_bullseye.ct': 152, 'characters/ts_zurgbots/ts_zurgbots.ct': 18,
             'characters/carriable/carriable.ct': 2, 'characters/googrowshrink/googrowshrink.ct': 28,
             'characters/ts_slinky/ts_slinky.ct': 51, 'characters/helicopter/helicopter.ct': 138,
             'characters/inc_gliderpack/inc_gliderpack.ct': 15, 'characters/ts_hamm/ts_hamm.ct': 22,
             'characters/mu_terry_terri/mu_terry_terri.ct': 150, 'characters/cars_animals/cars_animals.ct': 6,
             'characters/stickyhand/stickyhand.ct': 33, 'characters/inc_townspeople_vehicles/inc_townspeople_vehicles.ct': 5,
             'characters/avatar_tools/avatar_tools.ct': 76,
             'characters/tb_dragon/tb_dragon.ct': 31, 'characters/cars_windsock/cars_windsock.ct': 19, 'characters/jetpack/jetpack.ct': 7,
             'characters/lr_raven/lr_raven.ct': 15, 'characters/lr_flight_totem_backpack/lr_flight_totem_backpack.ct': 3,
             'characters/ts_rocketpack/ts_rocketpack.ct': 6, 'characters/inc_hoverboard/inc_hoverboard.ct': 6,
             'characters/ts_rexteleporter/ts_control_device.ct': 5}


def _u16s(r):
    cw = ((r.type >> 6) & 3) + 1
    b = r.pay[cw:]
    return list(struct.unpack_from('<%dH' % (len(b) // 2), b))


def links(path):
    """[(template, {port: 'header path|type'})] of every BaseDataNodeLinks in the file."""
    from oct import Oct
    b = open(path, 'rb').read()
    if b'DataNodeLinks' not in b and b'PortToLocalIndices' not in b:
        return []
    f = Oct.parse(b, path)
    out = []
    sns = [i for i, r in enumerate(f.recs) if r.level == 2 and r.name == 'SubNetwork']
    for k, s in enumerate(sns):
        e = sns[k + 1] if k + 1 < len(sns) else len(f.recs)
        hs = his = hl = cm = None
        dns, lks, cur, lk = [], [], None, None
        for r in f.recs[s + 1:e]:
            n = r.name
            if r.level == 3:
                cur = lk = None
                if n == 'HeaderStrings': hs = r.names()
                elif n == 'HeaderStringIndices': his = r.ints()
                elif n == 'HeaderLayout': hl = r.ints()
                elif n == 'ConnectionMapUsedDataNodes': cm = r.ints()
            elif r.level == 4:
                cur = lk = None
                if n == 'DataNode': dns.append({}); cur = dns[-1]
                elif n == 'BaseDataNodeLinks': lk = {}; lks.append(lk)
            elif r.level == 5:
                if cur is not None and n in ('Header', 'Type'): cur[n] = r.value()
                if lk is not None: lk[n] = r
        if not lks or hl is None or cm is None:
            continue
        parent = [-1]
        for i in range(0, len(hl) - 1, 2):
            parent += [hl[i]] * hl[i + 1]
        def hpath(h):
            p = []
            while h >= 1:
                p.append(hs[his[h - 1]] if h <= len(his) and his[h - 1] < len(hs) else '?')
                h = parent[h] if h < len(parent) else -1
            return '/'.join(reversed(p))
        def dn(i):
            d = dns[i] if 0 <= i < len(dns) else {}
            t = d.get('Type')
            return '%s|%s' % (hpath(d['Header']), t[0] if isinstance(t, list) else t) if 'Header' in d else '?'
        for lk in lks:
            ct = lk.get('ConnectionTemplate')
            if ct is None:
                continue
            pm = {}
            if 'DataNodeLinks' in lk:
                for p, x in enumerate(lk['DataNodeLinks'].ints()):
                    if 0 <= x < len(cm): pm[p] = dn(cm[x])
            elif 'PortToLocalIndices' in lk:
                lu, lp = _u16s(lk['LocalUsedDataNodeIndices']), _u16s(lk['LocalToPortIndices'])
                for i, p in enumerate(lp):
                    if i < len(lu) and lu[i] < len(cm): pm[p] = dn(cm[lu[i]])
            out.append((ct.value()[0].lower(), pm))
    return out


def _one(p):
    try:
        return p, links(p)
    except Exception as e:
        return p, 'ERR %r' % (e,)


def scan(root, outp, subs):
    from par import pmap
    files = []
    for sub in subs:
        for d, _, fs in os.walk(os.path.join(root, sub)):
            files += [os.path.join(d, x) for x in fs if x.endswith('.oct')]
    tm = collections.defaultdict(lambda: collections.defaultdict(collections.Counter))
    models = errs = 0
    for p, res in pmap(_one, files, chunksize=64):
        if isinstance(res, str):
            errs += 1
            continue
        models += bool(res)
        for ct, pm in res:
            for q, n in pm.items():
                tm[ct][q][n] += 1
    print('%s: %d .oct, %d with links, %d unreadable' % (root, len(files), models, errs))
    json.dump({ct: {str(q): dict(c) for q, c in v.items()} for ct, v in tm.items()}, open(outp, 'w'))


def norm(s):
    path = s.rsplit('|', 1)[0]
    return path.split('/', 1)[1].lower() if '/' in path else path.lower()


def names(d):
    """{port: name}: majority name per port; a name the majority at several ports stays only on its best-voted one."""
    best = {}
    for q, c in d.items():
        v = collections.Counter()
        for s, n in c.items():
            v[norm(s)] += n
        best[int(q)] = v.most_common(1)[0]
    keep = {}
    for q, (n, k) in best.items():
        if n not in keep or k > best[keep[n]][1]:
            keep[n] = q
    return {q: n for n, q in keep.items()}


DROP = 0xffff   # to30 value: the clip output is dropped, the model link skipped (cho.hpp)


def table(l10, l30, ct, n30):
    """to30 list over the 1.0 ports, counts by kind, and the shared spare (None if none is used). Spares are taken
    in port order; when they run out a 1.0-only name, and the unlinked ports, get DROP: 3.0 models skip such an id
    as they skip a spare, and every 1.0 model skips an unlinked port (DataNodeLinks -1); ids past n30 are not an
    option (3.0's sampler FUN_005329b0 maps an id through a table at evaluator +0x44 before any bounds check)."""
    a, b = names(l10.get(ct, {})), names(l30.get(ct, {}))
    linked30 = {int(q) for q in l30.get(ct, {})}
    n10 = 1 + max(int(q) for q in l10.get(ct, {}))
    inv = {n: q for q, n in b.items()}
    sp = iter([q for q in range(n30) if q not in linked30])
    to30, c = [], collections.Counter()

    def tail(x, y):   # y is x below one or more extra leading levels (a model root deeper than one level)
        return y.endswith('/' + x) and x.count('/') >= 1
    for p in range(n10):
        n = a.get(p)
        alt = [q for m, q in inv.items() if tail(m, n) or tail(n, m)] if n and n not in inv else []
        if p in a and a[p] in inv:
            to30.append(inv[a[p]]); c['same name'] += 1
        elif len(alt) == 1:
            to30.append(alt[0]); c['same name below another root level'] += 1
        elif p in a:
            q = next(sp, DROP)
            to30.append(q); c['1.0-only name -> own spare' if q != DROP else '1.0-only name -> dropped (no spare)'] += 1
        else:
            to30.append(None)
    sink = next(sp, DROP) if None in to30 else None
    c['unlinked in 1.0 -> shared spare' if sink != DROP else 'unlinked in 1.0 -> dropped (no spare)'] += to30.count(None)
    to30 = [sink if x is None else x for x in to30]
    kept = [x for x in to30 if x not in (sink, DROP)]
    assert len(set(kept)) == len(kept), 'two 1.0 ports on one 3.0 port'
    return to30, +c, sink


def header(f10, f30, outp):
    l10, l30 = json.load(open(f10)), json.load(open(f30))
    lines = ['// Generated by DI3Tools/portmap.py from the model link scans of both extracts; do not edit.',
             '// to30[1.0 port] = 3.0 port of the same connection name (portmap.py docstring has the rule);',
             '// kDropPort: no 3.0 port for it, the clip output is dropped and the model link skipped.',
             '#pragma once', '#include <cstdint>', '#include <cstring>', '', 'namespace cho {', '',
             'struct PortMap { const char* tmpl; uint32_t n10; const uint16_t* to30; };',
             'static const uint16_t kDropPort = 0x%x;' % DROP, '']
    rows = []
    for k, (ct, n30) in enumerate(TEMPLATES.items()):
        to30, c, sink = table(l10, l30, ct, n30)
        sk = 'none' if sink is None else 'dropped' if sink == DROP else sink
        print('%s: 1.0 ports %d -> 3.0 (%d ports): %s, shared spare %s' % (ct, len(to30), n30, dict(c), sk))
        lines.append('// %s: %s; shared spare %s' % (ct, ', '.join('%s %d' % kv for kv in c.items()), sk))
        lines.append('static const uint16_t kPorts%d[%d] = {' % (k, len(to30)))
        for i in range(0, len(to30), 20):
            lines.append('    ' + ', '.join(str(x) for x in to30[i:i + 20]) + ',')
        lines.append('};')
        rows.append('    {"%s", %d, kPorts%d},' % (ct, len(to30), k))
    lines += ['', 'static const PortMap kPortMaps[] = {'] + rows + ['};', '',
              '// The renumbering of a template 3.0 owns (path compared case-insensitively), or nullptr.',
              'inline const PortMap* portMap(const char* tmpl)',
              '{',
              '    for (const PortMap& m : kPortMaps)',
              '        if (!_stricmp(m.tmpl, tmpl)) return &m;',
              '    return nullptr;',
              '}', '', '}  // namespace cho', '']
    open(outp, 'w', newline='\n').write('\n'.join(lines))


if __name__ == '__main__':
    if len(sys.argv) >= 4 and sys.argv[1] == 'scan':
        scan(sys.argv[2], sys.argv[3], sys.argv[4:] or ['characters', 'choreographies'])
    elif len(sys.argv) == 5 and sys.argv[1] == 'header':
        header(*sys.argv[2:5])
    else:
        print(__doc__)
