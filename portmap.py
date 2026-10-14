"""1.0 -> 3.0 port numbering of the ConnectionTemplates both games own (motion-clip output ids, model links).

A motion clip's output id is a port of its LayerBind's ConnectionTemplate (3.0 FUN_00532050: id -> the model's
PortToLocalIndices -> DataNode; 1.0 FUN_005444c0: id -> DataNodeLinks). The template file itself holds no names
(.bct: Filters / Mirrors only); a port's connection is named by the models: BaseDataNodeLinks maps port -> local ->
ConnectionMapUsedDataNodes -> DataNode, whose header path without the model's root ('Anchor/Cntrl_Elbow_Right/
Transform' -> 'cntrl_elbow_right/transform', case-folded) is the name. Per game and template a port is named by the
majority over every model linking it (a name kept only on its best-voted port). to30[p] = the 3.0 port of 1.0 port
p's name; a 1.0 name 3.0 has no port for gets a distinct spare 3.0 port (one no 3.0 model links, so 3.0 models skip
it; a 1.0 model renumbered by the same table keeps that joint); a port no 1.0 model links shares one more spare.
Evidence (twin clips, the ids never used to build the table): rule.py in wt/_scratch/anim10, octtest --clip-ids.

usage: portmap.py scan <assets> <out.json> [sub ...]   models' port names (default subs: characters choreographies)
       portmap.py header <links10.json> <links30.json> <out.hpp>
Run scans under capped.ps1."""
import sys, os, json, collections, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# template -> 3.0 port count (Filter length of the 3.0 .bct, else the longest shipped PortToLocalIndices)
TEMPLATES = {'characters/biped/biped.ct': 968, 'characters/_townspeople/_townspeople.ct': 70,
             'characters/omnidroid/omnidroid.ct': 54}


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


def table(l10, l30, ct, n30):
    """to30 list over the 1.0 ports, and counts by kind."""
    a, b = names(l10.get(ct, {})), names(l30.get(ct, {}))
    linked30 = {int(q) for q in l30.get(ct, {})}
    n10 = 1 + max(int(q) for q in l10.get(ct, {}))
    inv = {n: q for q, n in b.items()}
    spares = [q for q in range(n30) if q not in linked30]
    to30, c, sp = [], collections.Counter(), iter(spares)
    for p in range(n10):
        if p in a and a[p] in inv:
            to30.append(inv[a[p]]); c['same name'] += 1
        elif p in a:
            to30.append(next(sp)); c['1.0-only name -> own spare'] += 1
        else:
            to30.append(None); c['unlinked in 1.0 -> shared spare'] += 1
    sink = next(sp)   # StopIteration = not enough spare ports: the table cannot be built for this template
    to30 = [sink if x is None else x for x in to30]
    assert len(set(to30) - {sink}) == len([x for x in to30 if x != sink]), 'two 1.0 ports on one 3.0 port'
    return to30, c, sink


def header(f10, f30, outp):
    l10, l30 = json.load(open(f10)), json.load(open(f30))
    lines = ['// Generated by DI3Tools/portmap.py from the model link scans of both extracts; do not edit.',
             '// to30[1.0 port] = 3.0 port of the same connection name (portmap.py docstring has the rule).',
             '#pragma once', '#include <cstdint>', '#include <cstring>', '', 'namespace cho {', '',
             'struct PortMap { const char* tmpl; uint32_t n10; const uint16_t* to30; };', '']
    rows = []
    for k, (ct, n30) in enumerate(TEMPLATES.items()):
        to30, c, sink = table(l10, l30, ct, n30)
        print('%s: 1.0 ports %d -> 3.0 (%d ports): %s, shared spare %d' % (ct, len(to30), n30, dict(c), sink))
        lines.append('// %s: %s; shared spare %d' % (ct, ', '.join('%s %d' % kv for kv in c.items()), sink))
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
