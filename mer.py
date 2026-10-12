"""Meridian gameplay graphs (.mer + its .merdebug twin) -- read, show, catalogue.

Decoded from the 3.0 extract: 640 .mer under assets/{worlds,exploders,realms}, each with a .merdebug twin.
584 are tuple files (oct.py); the other 56 (55 exploders, 1 realm) are EMPTY, 0 bytes, and so are their twins.
The twin is the same file plus _DebugOnly_ records.

MeridianOct
  MeridianNodePool > MNode = '<i>' (i = 0..N-1 in file order), Type = node class
    NameSpaceBegin .. NameSpaceEnd bracket each namespace: 1 per file in 568 files, up to 29 (merged layouts),
      never nested. Begin holds Name, GraphFileName, [UserNamespace], GraphVersion, RootNodeCount (= the nodes up
      to its End), EnableNetworking, [IsMerged, ReferenceEmbeddedNamespace, MergedLayoutPath] and the namespace's
      parameter blobs ParameterValues and ParameterStrings.
    every other node
      Mer_ParametersOffset      its block in the namespace's ParameterValues (empty blocks share the next offset)
      Mer_ParameterDescription  u32 n, n x (u8 type, u32 lookup2(name, seed 0), i32 offset in the block), sorted
                                by offset. 0 bool / 1 int / 2 float: 4 B; 3 string: u32 offset of a NUL-terminated
                                string in ParameterStrings + 8 x FF; 4 vec3: 3 floats
      NumInLinks, NumOutLinks, Mer_NoConnections (child nodes that only hang off a Parent)
      pins    <name> = <pin index>, for a SUBSET of the class's pins (often none; a name can repeat: DeathCloudNode
              Reset = 2 and 10); the numbering is per class, except the per-interface connection_* pins
      ports   <name> = <MNode index> instead, in 31 classes (EventSurge, LuaNode, NetSendInt, the camera family):
              their ports are sub-nodes (Event, DataFetch, DataSource.. carrying LeafNameHash) placed right before them.
              Sub-nodes list no pins; in links Event / DataSource / EventSurge send from pin 1, Handler / DataFetch
              receive at pin 0
      Parent (owner: NodeGroup, Choreography, SpawnActorNode..), MerSpawnGroup (SpawnGroupNode / NodeDefinition),
      LeafNameHash (lookup2 of the INTERNAL port name: 'Begin' behind an authored 'Entered~'), IsProxy = 1,
      Uuid (16 B, = _DebugOnly_Uuid), BoundObjectName (scene path, 'Layout|LOC_PlayerStart'), ResolveAnchorPathName
      (key BoundObjectName, 16 zero bytes), and on interface nodes (InterfaceUser, GraphInterface, NodeDefinition,
      Object) Mer_DebugOnly_DataTypeName, Mer_DataTypeNameHash (lookup2 of it lowercased) and
      Mer_ConnectionDescription: u32 n, n x (u32 type, u32 lookup2('connection_<pin>')), the node's connection_ pins
      in declaration (not pin) order. Links at those pins: type 0 input (impulse or data), 6 impulse output,
      2 / 3 data output, 8 / 9 data input
  MeridianLinkPool > MLink, per namespace that has links: NameSpaceBegin (Name ..), LinksBuffer, NameSpaceEnd, in
    node-pool order (one file has two namespaces of the same name)
    LinksBuffer.Data  u32 n, 00 03 00, n x (u16 code, u16 from, u16 to). from/to are positions in the namespace
                      (MNode index - Begin - 1); code = from pin << 10 | to pin << 4 | kind: 1 impulse, 3 data,
                      2 surge (a held state: Switch, CatalystSurge, AndSurge.. into SoundEmitter, cameras, emitters)
  MeridianNodeAssociationPool (2 files) > MNodeAssociation: Type Collection, Owner / Collector = MNode indices
.mtc (beside every world .mer: the engine's type manifest for it): u32 0xffff, u32 n node types, u32 m link types,
  (n + m) x (u32 count, u32 name offset), then the names (NUL-terminated). The node types are the .mer's MNode
  classes without NameSpaceBegin/End, sorted, with their counts; then its MLink types the same way (LinksBuffer,
  in the graphs that have links). mtc() rebuilds all 584 shipped .mtc byte-exact from their .mer. A .mtc that does
  not match its .mer declares node classes the graph does not have (a donor's .mtc copied beside another graph).
.merdebug adds per node _DebugOnly_Uuid, _DebugOnly_AuthoredName and one _DebugOnly_ParamValue = [name, value] per
description entry, in description order (a vec3 as the string '%f, %f, %f').

MEASURED by --selftest over the 584 graphs (130,221 nodes, 117,117 links, 428,387 parameters): every invariant above
holds in all of them -- descriptions of 128,913 of 128,913 nodes line up 1:1 with their .merdebug names (each of the
428,387 names hashes to its entry, and each decoded value equals the debug value), every parameter block lies
inside ParameterValues and the blocks cover it completely (654 of 654 namespaces), every link endpoint is a node of
its namespace, the links per node equal NumOutLinks / NumInLinks for every node, and no class mixes pins and ports.
Not decoded: names of pins a node does not list (867 of 36,805 link ends at nodes that list pins hit an unlisted
one; the catalogue names pins from the instances that list them), the internal name behind a LeafNameHash whose
authored label differs (24,495 of 43,868), and what the connection type codes mean beyond their link direction.

  g = load(path); for n in g['nodes']: print(n['index'], n['type'], n.get('params'))

python mer.py show <file.mer>                                 nodes with parameters, pins, ports and links
python mer.py mtc <file.mer> [<out.mtc>]                       the .mtc type manifest of a graph (default: beside it)
python mer.py catalogue [--ext30 DIR] [--out catalogue.json]   node types over every shipped .mer
python mer.py --selftest [--ext30 DIR]
"""
import argparse, json, re, struct, sys
from collections import Counter, defaultdict
from pathlib import Path
from oct import Oct, spec, is_tuple_file, SCALAR, INT, BYTE
from dct import lookup2

PTYPE = ('bool', 'int', 'float', 'string', 'vec3')
KIND = {1: 'impulse', 2: 'surge', 3: 'data'}
# node records that are neither pins nor ports; the decoded ones are left out of the node dict
META = {'Type', 'NumInLinks', 'NumOutLinks', 'Mer_NoConnections', 'Mer_ParametersOffset', 'Mer_ParameterDescription',
        'BoundObjectName', 'Parent', 'MerSpawnGroup', 'LeafNameHash', 'IsProxy', 'Uuid', 'ResolveAnchorPathName',
        'Mer_DataTypeNameHash', 'Mer_DebugOnly_DataTypeName', 'Mer_ConnectionDescription'}
NS_BLOBS = ('ParameterValues', 'ParameterStrings')
START = re.compile('Start|Spawn|Checkpoint|Ledge|Climb|Interact|Locator|Tardis|Catalyst|RealmCache|Respawn|Transport')


def h(name):
    """The name hash of descriptions, LeafNameHash and Mer_DataTypeNameHash."""
    return lookup2(name.encode('latin-1'), 0)


def blob(r):
    """Bytes of a blob record, key and count stripped."""
    s = spec(r.type)
    return bytes(r.pay[2 * s[1] + s[3]:])


def _val(r):
    if spec(r.type)[2] == BYTE:
        return blob(r).hex()
    v = r.value()
    return v[0] if isinstance(v, list) and len(v) == 1 else v


def _is_int(r):
    s = spec(r.type)
    return s[0] == SCALAR and not s[1] and s[2] == INT


def params(desc, pv, ps, base, names=()):
    """[{name, type, value}] of one node. names: the twin's _DebugOnly_ParamValue names in order; a name is used only
    if it hashes to its entry, else the name is '#<hash>'."""
    out = []
    for i in range(struct.unpack_from('<I', desc)[0]):
        t, hh, o = struct.unpack_from('<BIi', desc, 4 + 9 * i)
        q = base + o
        if t == 3:
            s = struct.unpack_from('<I', pv, q)[0]
            v = ps[s:ps.index(b'\0', s)].decode('latin-1')
        elif t == 4:
            v = struct.unpack_from('<3f', pv, q)
        else:
            v = struct.unpack_from(('<I', '<i', '<f')[t], pv, q)[0]
            v = bool(v) if t == 0 else v
        nm = names[i] if i < len(names) and h(names[i]) == hh else '#%08x' % hh
        out.append({'name': nm, 'type': PTYPE[t], 'value': v})
    return out


def load(path):
    """-> {'path', 'debug', 'namespaces', 'nodes', 'links', 'associations'}. nodes[i] is MNode i: index, type, ns
    (namespace index), name (authored, from the twin), params, pins {pin: name}, ports {name: MNode index}, plus its
    other records by their own names (NumInLinks, BoundObjectName, Parent, LeafNameHash ...; blobs as hex).
    links: {from, from_pin, to, to_pin, kind} with MNode indices (+ from_pin_name / to_pin_name when listed)."""
    path = Path(path)
    f = Oct.load(path)
    twin = path.with_suffix('.merdebug')
    g = f if path.suffix == '.merdebug' else Oct.load(twin) if twin.exists() and is_tuple_file(twin) else None
    G = {'path': str(path), 'debug': g is not None, 'namespaces': [], 'nodes': [], 'links': [], 'associations': []}
    gn = g.find(name='MNode') if g else []
    ns = pv = ps = None
    for i, n in enumerate(f.find(name='MNode')):
        a, b = f.block(n)
        recs = [r for r in f.recs[a + 1:b] if not r.name.startswith('_DebugOnly_')]
        k = {r.name: r for r in recs}
        t = k['Type'].names()[0]
        node = {'index': i, 'type': t}
        if t == 'NameSpaceBegin':
            ns = dict(index=len(G['namespaces']), begin=i, **{r.name: _val(r) for r in recs if r.name not in NS_BLOBS
                                                              and r.name != 'Type'})
            pv, ps = blob(k['ParameterValues']), blob(k['ParameterStrings'])
            G['namespaces'].append(ns)
        node['ns'] = ns['index'] if ns else None
        if t == 'NameSpaceEnd':
            ns['end'] = i
        if t not in ('NameSpaceBegin', 'NameSpaceEnd'):
            ga, gb = g.block(gn[i]) if g else (0, 0)
            dbg = g.recs[ga + 1:gb] if g else []
            node['name'] = next((r.names()[0] for r in dbg if r.name == '_DebugOnly_AuthoredName'), None)
            node['_ints'] = [(r.name, r.value()) for r in recs if r.name not in META and _is_int(r)]
            node.update({r.name: _val(r) for r in recs if not (r.name not in META and _is_int(r)) and r.name not in
                         ('Type', 'Mer_ParameterDescription', 'Mer_ConnectionDescription')})
            if 'LeafNameHash' in node:
                node['LeafNameHash'] &= 0xffffffff
            if 'Mer_ConnectionDescription' in k:
                d = blob(k['Mer_ConnectionDescription'])
                node['connections'] = [list(struct.unpack_from('<II', d, 4 + 8 * j))
                                       for j in range(struct.unpack_from('<I', d)[0])]
            if 'Mer_ParameterDescription' in k:
                names = [r.key for r in dbg if r.name == '_DebugOnly_ParamValue']
                node['params'] = params(blob(k['Mer_ParameterDescription']), pv, ps, k['Mer_ParametersOffset'].value(),
                                        names)
        G['nodes'].append(node)
    N = G['nodes']
    for n in N:     # named ints are sub-node refs when all point back at port nodes of the namespace, else pins
        ints = n.pop('_ints', None)
        if ints is not None:
            port = ints and all(0 <= v < n['index'] and N[v]['ns'] == n['ns'] and 'LeafNameHash' in N[v] and
                                'Parent' not in N[v] for _, v in ints)
            n['ports'], n['pins'] = (dict(ints), {}) if port else ({}, {v: nm for nm, v in ints})
    byname = defaultdict(list)
    for s in G['namespaces']:
        byname[s.get('Name')].append(s)
    cur = None
    for m in f.find(name='MLink'):
        k = f.kids(m)
        t = k['Type'].names()[0]
        if t == 'NameSpaceBegin':
            cur = byname[_val(k['Name'])].pop(0)       # same order as the node pool; one name is used twice
        elif t == 'LinksBuffer':
            d = blob(k['Data'])
            for j in range(struct.unpack_from('<I', d)[0]):
                c, a, z = struct.unpack_from('<HHH', d, 7 + 6 * j)
                L = {'from': cur['begin'] + 1 + a, 'from_pin': c >> 10, 'to': cur['begin'] + 1 + z,
                     'to_pin': c >> 4 & 63, 'kind': KIND.get(c & 15, c & 15)}
                for end in ('from', 'to'):
                    nm = N[L[end]].get('pins', {}).get(L[end + '_pin'])
                    if nm:
                        L[end + '_pin_name'] = nm
                G['links'].append(L)
    for m in f.find(name='MNodeAssociation'):
        k = f.kids(m)
        if k['Type'].names()[0] not in ('NameSpaceBegin', 'NameSpaceEnd'):
            G['associations'].append({r.name: _val(r) for r in k.values()})
    return G


# --- show ----------------------------------------------------------------------------------------------------
def _fmt(v):
    if isinstance(v, float):
        return repr(float('%.7g' % v))
    if isinstance(v, (tuple, list)):
        return '(%s)' % ', '.join(_fmt(x) for x in v)
    return repr(v) if isinstance(v, str) else str(v)


def _pin(p, name):
    return '%s(%d)' % (name, p) if name else '#%d' % p


def show(path):
    G = load(path)
    N = G['nodes']
    out = defaultdict(list)
    for L in G['links']:
        out[L['from']].append(L)
    print('== %s  %d namespaces, %d nodes, %d links, parameter names %s'
          % (Path(path).name, len(G['namespaces']), len(N), len(G['links']),
             'from the .merdebug' if G['debug'] else 'unknown (no .merdebug: #hash)'))
    skip = {'index', 'type', 'ns', 'name', 'params', 'pins', 'ports', 'connections', 'Mer_ParametersOffset'}
    for n in N:
        if n['type'] == 'NameSpaceBegin':
            s = G['namespaces'][n['ns']]
            print('namespace %d: %s' % (s['index'], '  '.join('%s=%s' % (k, v) for k, v in s.items()
                                                             if k not in ('index', 'begin', 'end'))))
            continue
        if n['type'] == 'NameSpaceEnd':
            continue
        print('%5d %s%s  %s' % (n['index'], n['type'], ' "%s"' % n['name'] if n.get('name') else '',
                                '  '.join('%s=%s' % (k, v) for k, v in n.items() if k not in skip)))
        if n.get('params'):
            print('        params ' + '  '.join('%s=%s' % (p['name'], _fmt(p['value'])) for p in n['params']))
        if n.get('pins'):
            print('        pins   ' + '  '.join('%s=%d' % (nm, i) for i, nm in sorted(n['pins'].items())))
        if n.get('ports'):
            print('        ports  ' + '  '.join('%s=%d %s' % (k, v, N[v]['type']) for k, v in n['ports'].items()))
        for L in out[n['index']]:
            print('        %-7s %s -> %d %s.%s' % (L['kind'], _pin(L['from_pin'], L.get('from_pin_name')), L['to'],
                                               N[L['to']]['type'], _pin(L['to_pin'], L.get('to_pin_name'))))
    for a in G['associations']:
        print('association %s' % a)


# --- catalogue -----------------------------------------------------------------------------------------------
def _key(v):
    """Hashable, rounded form of a parameter value (float32 noise would split equal values)."""
    if isinstance(v, float):
        return float('%.7g' % v)
    return tuple(_key(x) for x in v) if isinstance(v, (tuple, list)) else v


def _tally(p):
    """(world row, {type: counts}) of one graph -- worker side of the catalogue."""
    G = load(p)
    N = G['nodes']
    T = {}
    for n in N:
        d = T.setdefault(n['type'], {'n': 0, 'params': {}, 'pins': Counter(), 'ports': Counter(), 'in': Counter(),
                                     'out': Counter(), 'bound': Counter(), 'fields': Counter()})
        d['n'] += 1
        for q in n.get('params', ()):
            d['params'].setdefault(q['name'], Counter())[(q['type'], _key(q['value']))] += 1
        d['pins'].update((nm, i) for i, nm in n.get('pins', {}).items())
        d['ports'].update((k, N[v]['type']) for k, v in n.get('ports', {}).items())
        if 'BoundObjectName' in n:
            d['bound'][n['BoundObjectName']] += 1
        d['fields'].update(k for k in n if k not in ('index', 'type', 'ns', 'name', 'params', 'pins', 'ports'))
    for L in G['links']:
        a, z = N[L['from']], N[L['to']]
        T[a['type']]['out'][(L['kind'], L['from_pin'], L.get('from_pin_name'), z['type'], L['to_pin'],
                             L.get('to_pin_name'))] += 1
        T[z['type']]['in'][(L['kind'], L['to_pin'], L.get('to_pin_name'), a['type'], L['from_pin'],
                            L.get('from_pin_name'))] += 1
    p = Path(p)
    s = G['namespaces'][0] if G['namespaces'] else {}
    names = {str(s.get(k, '')).lower() for k in ('Name', 'UserNamespace', 'GraphFileName')}
    world = {'file': '/'.join(p.parts[-4:] if p.parts[-4] == 'exploders' else p.parts[-3:]), 'folder': p.parent.name,
             'nodes': len(N), 'links': len(G['links']),
             'namespaces': [[x.get('Name'), x.get('UserNamespace')] for x in G['namespaces']],
             'matches_folder': p.parent.name.lower() in names}
    return world, T


def catalogue(files):
    from par import pmap
    res = pmap(_tally, files, chunksize=4)
    raw = {}
    for w, T in res:
        for t, d in T.items():
            a = raw.setdefault(t, {'n': 0, 'files': 0, 'worlds': 0, 'params': {}, 'pins': Counter(), 'ports': Counter(),
                                   'in': Counter(), 'out': Counter(), 'bound': Counter(), 'fields': Counter()})
            a['n'] += d['n']
            a['files'] += 1
            a['worlds'] += w['file'].startswith('worlds/')
            for k in ('pins', 'ports', 'in', 'out', 'bound', 'fields'):
                a[k].update(d[k])
            for nm, c in d['params'].items():
                a['params'].setdefault(nm, Counter()).update(c)
    learned = defaultdict(lambda: defaultdict(set))     # type -> pin index -> the names instances give it
    for t, a in raw.items():
        for nm, p in a['pins']:
            learned[t][p].add(nm)

    def pin(t, p, nm):
        return _pin(p, nm or '|'.join(sorted(learned[t][p])))

    def links(t, c, out):
        """The 8 most common link shapes, 'kind mypin -> Type.pin' (out) or 'kind Type.pin -> mypin' (in)."""
        r = Counter()
        for (k, mp, mn, ot, op, on), n in c.items():
            me, other = pin(t, mp, mn), '%s.%s' % (ot, pin(ot, op, on))
            r['%s %s -> %s' % ((k, me, other) if out else (k, other, me))] += n
        return [list(x) for x in r.most_common(8)]

    types = {}
    for t, a in sorted(raw.items(), key=lambda kv: -kv[1]['n']):
        ps = {}
        for nm, c in a['params'].items():
            ty, vals = Counter(), Counter()
            for (pt, v), k in c.items():
                ty[pt] += k
                vals[v] += k
            ps[nm] = {'type': '|'.join(sorted(ty)), 'count': sum(c.values()), 'distinct': len(vals),
                      'values': [[v, k] for v, k in vals.most_common(5)]}
        pins = defaultdict(dict)
        for (nm, p), k in a['pins'].items():
            pins[p][nm] = k
        types[t] = {'count': a['n'], 'files': a['files'], 'worlds': a['worlds'], 'params': ps,
                    'pins': {p: (next(iter(v)) if len(v) == 1 else v) for p, v in sorted(pins.items())},
                    'ports': sorted('%s -> %s' % kv for kv in a['ports']),
                    'links_in': sum(a['in'].values()), 'links_out': sum(a['out'].values()),
                    'in': links(t, a['in'], False), 'out': links(t, a['out'], True),
                    'bound': [[b, c] for b, c in a['bound'].most_common(5)], 'bound_count': sum(a['bound'].values()),
                    'fields': dict(a['fields'].most_common())}
    return {'files': len(files), 'nodes': sum(w['nodes'] for w, _ in res), 'links': sum(w['links'] for w, _ in res),
            'types': types, 'worlds': [w for w, _ in res]}


def _summary(cat):
    T = cat['types']
    m = Counter((w['file'].split('/')[0], w['matches_folder']) for w in cat['worlds'])
    print('%d graphs, %d nodes, %d links, %d node types; first namespace = folder name (ignoring case) in %s'
          % (cat['files'], cat['nodes'], cat['links'], len(T),
             ', '.join('%d of %d %s' % (m[d, True], m[d, True] + m[d, False], d) for d in sorted({d for d, _ in m}))))
    row = '%-34s %7s %5s %6s %6s  %s'
    print(row % ('type', 'nodes', 'files', 'in', 'out', 'parameters'))

    def line(t, d):
        print(row % (t, d['count'], d['files'], d['links_in'], d['links_out'], ' '.join(d['params'])[:90]))
    for t in list(T)[:40]:
        line(t, T[t])
    print('\nstart / spawn / interaction related (/%s/):' % START.pattern)
    for t, d in T.items():
        if START.search(t):
            line(t, d)


# --- selftest ------------------------------------------------------------------------------------------------
def _check(p):
    """(Counter {(check, 0): tried, (check, 1): held}, [failures], {type: 'pins'|'ports'}) for one graph."""
    c, bad, kinds = Counter(), [], defaultdict(set)

    def chk(what, ok, why=''):
        c[what, 0] += 1
        c[what, 1] += bool(ok)
        if not ok and len(bad) < 5:
            bad.append('%s: %s %s' % (p.name, what, why))
    f, g = Oct.load(p), Oct.load(p.with_suffix('.merdebug'))
    G = load(p)
    N = G['nodes']
    fn, gn = f.find(name='MNode'), g.find(name='MNode')
    chk('MNode names = 0..N-1 in order', [int(n.names()[0]) for n in fn] == list(range(len(fn))))
    chk('.merdebug has the same nodes', [g.kids(x)['Type'].names()[0] for x in gn] == [n['type'] for n in N])
    for x, n in zip(fn, N):
        recs = f.recs[f.block(x)[0] + 1:f.block(x)[1]]
        rep = {nm for nm, k in Counter(r.name for r in recs).items() if k > 1}
        chk('a record name repeats only as a pin', all(_is_int(r) and r.name not in META for r in recs if r.name in rep))
        ints = [r for r in recs if r.name not in META and _is_int(r)]
        if n['type'] not in ('NameSpaceBegin', 'NameSpaceEnd'):
            chk('pin indices / port names unique per node', len(n['pins']) + len(n['ports']) == len(ints))
    depth = 0
    for n in N:
        depth += n['type'] == 'NameSpaceBegin'
        chk('namespaces never nested', depth <= 1)
        chk('every node inside a namespace', depth == 1, n['index'])
        depth -= n['type'] == 'NameSpaceEnd'
    for s in G['namespaces']:
        chk('RootNodeCount = nodes in the namespace', s.get('RootNodeCount') == s['end'] - s['begin'] - 1)
    links_out, links_in = Counter(), Counter()
    for L in G['links']:
        chk('link endpoints in one namespace', N[L['from']]['ns'] == N[L['to']]['ns'] and
            N[L['from']]['type'] not in ('NameSpaceBegin', 'NameSpaceEnd') and
            N[L['to']]['type'] not in ('NameSpaceBegin', 'NameSpaceEnd'))
        chk('link kind is 1, 2 or 3', L['kind'] in KIND.values())
        links_out[L['from']] += 1
        links_in[L['to']] += 1
        for end in ('from', 'to'):
            if N[L[end]].get('pins'):
                c['link pins listed by a node that lists pins', 0] += 1
                c['link pins listed by a node that lists pins', 1] += end + '_pin_name' in L
    for m in f.find(name='MLink'):
        k = f.kids(m)
        if 'Data' in k:
            d = blob(k['Data'])
            chk('LinksBuffer = u32 n, 00 03 00, n x 6 B', d[4:7] == b'\0\3\0' and
                len(d) == 7 + 6 * struct.unpack_from('<I', d)[0])
    pv = ps = cover = None
    for n, x, y in zip(N, fn, gn):
        k = f.kids(x)
        if n['type'] == 'NameSpaceBegin':
            if cover is not None:
                chk('node blocks cover ParameterValues', all(cover))
            pv, ps = blob(k['ParameterValues']), blob(k['ParameterStrings'])
            cover = [False] * len(pv)
            continue
        if n['type'] == 'NameSpaceEnd':
            continue
        chk('links per node = NumOutLinks / NumInLinks', links_out[n['index']] == n.get('NumOutLinks', 0) and
            links_in[n['index']] == n.get('NumInLinks', 0), n['index'])
        chk('no records besides pins, ports and the known ones', set(n) <= {'index', 'type', 'ns', 'name', 'params',
            'pins', 'ports', 'connections'} | META, set(n) - META)
        for r in ('Parent', 'MerSpawnGroup'):
            if r in n:
                chk('Parent / MerSpawnGroup is a node of the namespace', 0 <= n[r] < len(N) and N[n[r]]['ns'] == n['ns'])
        if n.get('pins') or n.get('ports'):
            kinds[n['type']].add('ports' if n.get('ports') else 'pins')
        chk('every node has Mer_ParametersOffset + Mer_ParameterDescription', 'params' in n, n['index'])
        if 'params' not in n:
            continue
        desc = blob(k['Mer_ParameterDescription'])
        cnt = struct.unpack_from('<I', desc)[0]
        ent = [struct.unpack_from('<BIi', desc, 4 + 9 * i) for i in range(cnt)]
        chk('description = u32 n + n x 9 B, sorted by offset', len(desc) == 4 + 9 * cnt and
            ent == sorted(ent, key=lambda e: e[2]))
        dv = [r.value() for r in g.recs[g.block(y)[0] + 1:g.block(y)[1]] if r.name == '_DebugOnly_ParamValue']
        chk('.merdebug names every description entry', len(dv) == cnt, n['index'])
        base = n['Mer_ParametersOffset']
        for (t, hh, o), q, d in zip(ent, n['params'], dv):
            size = 12 if t in (3, 4) else 4
            chk('parameter type is 0..4', t < 5, t)
            chk('parameter block inside ParameterValues', 0 <= base + o and base + o + size <= len(pv))
            for i in range(base + o, min(base + o + size, len(pv))):
                cover[i] = True
            chk('.merdebug name hashes to the entry', h(d[0]) == hh and q['name'] == d[0], d[0])
            if t == 3:
                s = struct.unpack_from('<I', pv, base + o)[0]
                chk('string: offset in ParameterStrings, NUL-terminated, + 8 x FF', s < len(ps) and b'\0' in ps[s:] and
                    pv[base + o + 4:base + o + 12] == b'\xff' * 8)
            if t == 0:
                chk('bool is 0 or 1', struct.unpack_from('<I', pv, base + o)[0] in (0, 1))
            v = q['value']
            chk('decoded value = .merdebug value', ('%f, %f, %f' % v if t == 4 else v) == d[1], (q, d))
        dk = g.kids(y)
        if 'Uuid' in n:
            chk('Uuid = _DebugOnly_Uuid', n['Uuid'] == blob(dk['_DebugOnly_Uuid']).hex())
        if 'Mer_DataTypeNameHash' in n:
            chk('Mer_DataTypeNameHash = lookup2(DataTypeName lowercased)',
                n['Mer_DataTypeNameHash'] & 0xffffffff == h(n['Mer_DebugOnly_DataTypeName'].lower()))
        if 'connections' in n:
            chk('Mer_ConnectionDescription hashes = lookup2 of the connection_ pins (as a set)',
                sorted(x[1] for x in n['connections']) == sorted(h(nm) for nm in n['pins'].values()))
        if 'LeafNameHash' in n and n.get('name'):
            leaf = n['name'].split(' / ')[-1]
            c['LeafNameHash = lookup2(authored leaf label)', 0] += 1
            c['LeafNameHash = lookup2(authored leaf label)', 1] += n['LeafNameHash'] in (h(leaf), h(leaf.rstrip('~')))
    if cover is not None:
        chk('node blocks cover ParameterValues', all(cover))
    for a in G['associations']:
        chk('association Owner / Collector are nodes', all(0 <= a[r] < len(N) for r in ('Owner', 'Collector')))
    c['graphs', 0] += 1
    c['nodes', 0] += len(N)
    c['links', 0] += len(G['links'])
    c['parameters', 0] += sum(len(n.get('params', ())) for n in N)
    return c, bad, dict(kinds)


INFO = ('link pins listed by a node that lists pins', 'LeafNameHash = lookup2(authored leaf label)')


MARKERS = ('NameSpaceBegin', 'NameSpaceEnd')


def mtc(o):
    """The .mtc type manifest of a .mer (an Oct, or a path)."""
    o = o if isinstance(o, Oct) else Oct.load(o)
    sections = []
    for pool, rec in (('MeridianNodePool', 'MNode'), ('MeridianLinkPool', 'MLink')):
        c = Counter(o.kids(r)['Type'].names()[0] for r in o.find(name=rec, pool=pool))
        sections.append(sorted((t, n) for t, n in c.items() if t not in MARKERS))
    ents = sections[0] + sections[1]
    at, table, names = 12 + 8 * len(ents), b'', b''
    for t, n in ents:
        table += struct.pack('<II', n, at + len(names))
        names += t.encode('latin-1') + b'\0'
    return struct.pack('<III', 0xffff, len(sections[0]), len(sections[1])) + table + names


def _mtc_exact(p):
    t = p.with_suffix('.mtc')
    return None if not t.exists() else mtc(p) == t.read_bytes()


def _selftest():
    import paths
    from par import pmap
    allf = sorted(paths.ext30().rglob('*.mer'))
    assert allf, 'no .mer under ' + str(paths.ext30())
    empty = [p for p in allf if p.stat().st_size == 0 and p.with_suffix('.merdebug').stat().st_size == 0]
    files = [p for p in allf if is_tuple_file(p) and is_tuple_file(p.with_suffix('.merdebug'))]
    print('%d .mer: %d tuple files with a tuple .merdebug twin, %d empty (0 B, twin too: %s), %d other'
          % (len(allf), len(files), len(empty), dict(Counter(p.relative_to(paths.ext30()).parts[0] for p in empty)),
             len(allf) - len(files) - len(empty)))
    c, bad, kinds = Counter(), [], defaultdict(set)
    for ci, bi, ki in pmap(_check, files, chunksize=4):
        c.update(ci)
        bad += bi
        for t, s in ki.items():
            kinds[t] |= s
    for what in ('graphs', 'nodes', 'links', 'parameters'):
        print('%-12s %s' % (what, format(c.pop((what, 0)), ',')))
    for what in sorted({w for w, _ in c}):
        print('  %s %s of %s' % ('info' if what in INFO else ('ok  ' if c[what, 0] == c[what, 1] else 'FAIL'),
                                 format(c[what, 1], ','), format(c[what, 0], ',')), what)
    mixed = [t for t, s in kinds.items() if len(s) > 1]
    print('  %s %d node types list ports (sub-node refs), %d list pins, %d both'
          % ('ok  ' if not mixed else 'FAIL', sum(s == {'ports'} for s in kinds.values()),
             sum(s == {'pins'} for s in kinds.values()), len(mixed)))
    for b in bad[:20]:
        print('  FAIL', b)
    mt = Counter(pmap(_mtc_exact, files, chunksize=8))
    print('  %s %d of %d .mtc rebuilt byte-exact from their .mer' % ('ok  ' if not mt[False] else 'FAIL', mt[True],
                                                                   mt[True] + mt[False]))
    assert len(empty) + len(files) == len(allf), 'a .mer that is neither empty nor a tuple file'
    assert not mt[False], '.mtc mismatches'
    assert not mixed and all(c[w, 0] == c[w, 1] for w, _ in c if w not in INFO), 'failures above'


if __name__ == '__main__':
    import paths
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0], epilog=__doc__.split('\n\n')[-1],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('cmd', choices=('show', 'catalogue', 'mtc'), nargs='?')
    ap.add_argument('file', nargs='?', help='show / mtc: the .mer (its .merdebug twin names the parameters)')
    ap.add_argument('dest', nargs='?', help='mtc: where to write it (default: beside the .mer)')
    ap.add_argument('--out', help='catalogue: write the full catalogue as JSON here')
    ap.add_argument('--selftest', action='store_true')
    paths.add_args(ap, 'ext30')
    a = paths.use(ap.parse_args())
    sys.stdout.reconfigure(encoding='utf-8')
    if a.selftest:
        _selftest()
    elif a.cmd == 'show' and a.file:
        show(a.file)
    elif a.cmd == 'mtc' and a.file:
        dest = Path(a.dest) if a.dest else Path(a.file).with_suffix('.mtc')
        dest.write_bytes(mtc(a.file))
        print('wrote', dest)
    elif a.cmd == 'catalogue':
        cat = catalogue([p for p in sorted(paths.ext30().rglob('*.mer')) if is_tuple_file(p)])
        _summary(cat)
        if a.out:
            Path(a.out).write_text(json.dumps(cat, indent=1), encoding='utf-8')
            print('wrote', a.out)
    else:
        ap.print_help()
