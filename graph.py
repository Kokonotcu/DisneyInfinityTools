"""Resize a world's Meridian graph to carry N geometry nodes.

Four launches crashed at OctanePlayerMain+0xdcddd7 with eax = 0 because scene
tree nodes were added to a SubNetwork sized for exactly one. The graph is not
descriptive — it is allocated, and every geometry node owns a slice of it.

The shape is fully determined by three shipped files that agree exactly
(dor_bay, dor_intro, mba_holo_art_test_jeffz, all n=2, vs out_intro2 at n=1):

  headers   Layout, Visibility, Transform, then one per node NAME, then per
            node a 5-block: Transform, In WorldBasis.Transform, In WorldBasis,
            Convert To WorldBasis, ConvertDilationToTransform
  datanodes 2 shared (Visibility, Dilation) + 3 per node
  procnodes 2 per node, VariantGroup 1 per node
  layout    (header, childCount) pairs; Layout's child count is n + 2

run  python graph.py --selftest   to re-derive out_intro2 and dor_bay from the
rule and assert every count, HeaderStringIndices and HeaderLayout matches.
"""
import struct, sys
from pathlib import Path
from oct import Oct, Rec

FIXED = ['Layout', 'Visibility', 'Transform', 'In WorldBasis.Transform',
         'In WorldBasis', 'Convert To WorldBasis', 'ConvertDilationToTransform']
BLOCK = ['Transform', 'In WorldBasis.Transform', 'In WorldBasis',
         'Convert To WorldBasis', 'ConvertDilationToTransform']


def plan(n, names, root='Layout'):
    """Every derived number for an n-geometry-node graph."""
    assert len(names) == n
    b = lambda i: 4 + n + 5 * i          # 1-based header of node i's block
    hdr = [root, 'Visibility', 'Transform'] + list(names)
    for i in range(n):
        hdr += BLOCK
    layout = [0, 1, 1, n + 2]
    for i in range(n):
        layout += [4 + i, 1, b(i), 3, b(i) + 1, 1]
    return {
        'headers': hdr,                        # header k (1-based) -> string
        'VariableHeaderCount': 3 + n + 5 * n,
        'VariableDataNodeCount': 2 + 3 * n,
        'ProcessorNodeCount': 2 * n,
        'VariantGroupCount': n,
        'VariantMemberCount': 2 * n,
        'HeaderLayout': layout,
        # per node i: DataNodes 2+3i (Dilation/BasisRef1), 3+3i (Transform),
        # 4+3i (Dilation); processors and the variant group reference them
        'datanode_headers': [2, 3] + [h for i in range(n)
                                      for h in (b(i), b(i) + 1, b(i) + 2)],
        'proc': [(b(i) + 4, [4 + 3 * i, 3 + 3 * i],
                  b(i) + 3, [2 + 3 * i, 1, 4 + 3 * i]) for i in range(n)],
        'variant': [[4 + 3 * i, 3 + 3 * i] for i in range(n)],
        'acc32': {'1': [4, 36 + 148 * n, 2 + 5 * n], '4': [4, 12 * n, 2 * n]},
        'acc64': {'1': [8, 40 + 168 * n, 2 + 5 * n], '4': [8, 24 * n, 2 * n]},
    }


def _kids(f, rec):
    i, j = f.block(rec)
    return {r.name: r for r in f.recs[i + 1:j]}


def _set_nameidx(f, rec, strings):
    """Type 0x0A is u8 count + u16 name indices — set_ints would retype it."""
    idx = [f.add_name(s) for s in strings]
    rec.pay = bytes([len(idx)]) + b''.join(i.to_bytes(2, 'little') for i in idx)


def expand(f, names, root='Layout'):
    """Resize f's SubNetwork graph to len(names) geometry nodes."""
    n = len(names)
    p = plan(n, names, root)

    for k in ('VariableHeaderCount', 'VariableDataNodeCount', 'ProcessorNodeCount',
              'VariantGroupCount', 'VariantMemberCount'):
        f.find1(name=k).set_int(p[k])

    uniq = []
    for s in p['headers']:
        if s not in uniq:
            uniq.append(s)
    _set_nameidx(f, f.find1(name='HeaderStrings'), uniq)
    f.find1(name='HeaderStringIndices').set_ints([uniq.index(s) for s in p['headers']])
    f.find1(name='HeaderLayout').set_ints(p['HeaderLayout'])

    for w in ('32', '64'):
        rec = f.find1(name='VariableAccumulatedSize' + w)
        i, j = f.block(rec)
        for r in f.recs[i + 1:j]:
            if r.name in p['acc' + w]:
                r.set_ints(p['acc' + w][r.name])

    # --- DataNodePool: 2 shared, then one cloned triple per node ------------
    dns = f.find(name='DataNode')
    triple = [f.clone_block(d) for d in dns[2:5]]
    out = []
    for i, d in enumerate(dns[:2]):
        out += f.clone_block(d)
    for i in range(n):
        for t, proto in enumerate(triple):
            blk = [Rec(f, r.level, r.type, r.name_idx, bytes(r.pay)) for r in proto]
            blk[0].pay = f.add_name(str(2 + 3 * i + t)).to_bytes(2, 'little')
            _kids_of(f, blk)['Header'].set_int(p['datanode_headers'][2 + 3 * i + t])
            # The per-node Dilation carries its position. out_intro2's single
            # node sits at the origin so its exporter omitted the whole block;
            # cloning that left a null where the engine dereferences one, which
            # is the eax = 0 fault. lb_zonetwo gives every node's Dilation a
            # Data{Translation}. Placement is baked into the vertices here, so
            # the value is zero -- it is the record's presence that matters.
            if t == 0 and 'Data' not in _kids_of(f, blk):
                lv = blk[0].level
                blk.append(Rec(f, lv + 1, 0x001, f.add_name('Data'), b''))
                blk.append(Rec(f, lv + 2, 0x012, f.add_name('Translation'),
                               bytes([3]) + struct.pack('<3f', 0.0, 0.0, 0.0)))
            out += blk
    _splice_pool(f, 'DataNodePool', out)

    # --- ProcessorNodePool --------------------------------------------------
    pns = f.find(name='ProcessorNode')
    protos = [f.clone_block(x) for x in pns[:2]]
    out = []
    for i, (h0, r0, h1, r1) in enumerate(p['proc']):
        for t, (hh, rr) in enumerate(((h0, r0), (h1, r1))):
            blk = [Rec(f, r.level, r.type, r.name_idx, bytes(r.pay)) for r in protos[t]]
            blk[0].pay = f.add_name(str(2 * i + t)).to_bytes(2, 'little')
            k = _kids_of(f, blk)
            k['Header'].set_int(hh)
            k['DataNodeRefs'].set_ints(rr)
            out += blk
    _splice_pool(f, 'ProcessorNodePool', out)

    # --- VariantGroupPool ---------------------------------------------------
    vgs = f.find(name='VariantGroup')
    out = []
    for v in p['variant']:
        r = Rec(f, vgs[0].level, vgs[0].type, vgs[0].name_idx, bytes(vgs[0].pay))
        r.set_ints(v)
        out.append(r)
    _splice_pool(f, 'VariantGroupPool', out)
    return p


def _kids_of(f, blk):
    return {r.name: r for r in blk[1:]}


def _splice_pool(f, pool_name, new_recs):
    pool = f.find1(name=pool_name)
    i, j = f.block(pool)
    f.recs[i + 1:j] = new_recs


def _selftest():
    """Re-derive two shipped files from the rule; any drift fails here."""
    W = Path(r'D:\DI-Extract\3.0\assets\worlds')
    # header 1 is the parent transform's own name, not the literal "Layout"
    cases = [('out_intro2', ['Default_Floor'], 'Layout'),
             ('dor_bay', ['COL_TerrainBackground', 'COL_TerrainBorder'], 'Layout'),
             ('lb_zonetwo', ['Floor', 'Wall1', 'Wall3'], 'LB_ZoneTwo1')]
    for name, nodes, root in cases:
        f = Oct.load(W / name / (name + '.oct'))
        p = plan(len(nodes), nodes, root)
        for k in ('VariableHeaderCount', 'VariableDataNodeCount', 'ProcessorNodeCount',
                  'VariantGroupCount', 'VariantMemberCount'):
            got = f.find1(name=k).ints()[0]
            assert got == p[k], '%s %s: derived %d, file %d' % (name, k, p[k], got)
        assert f.find1(name='HeaderLayout').ints() == p['HeaderLayout'], \
            '%s HeaderLayout: derived %s, file %s' % (name, p['HeaderLayout'],
                                                      f.find1(name='HeaderLayout').ints())
        hs = f.find1(name='HeaderStrings').names()
        hi = f.find1(name='HeaderStringIndices').ints()
        assert [hs[i] for i in hi] == p['headers'], \
            '%s headers: derived %s, file %s' % (name, p['headers'], [hs[i] for i in hi])
        for w in ('32', '64'):
            rec = f.find1(name='VariableAccumulatedSize' + w)
            i, j = f.block(rec)
            got = {r.name: r.ints() for r in f.recs[i + 1:j]}
            assert got == p['acc' + w], '%s acc%s: derived %s, file %s' % (
                name, w, p['acc' + w], got)
        # processors and variant groups
        for i, (h0, r0, h1, r1) in enumerate(p['proc']):
            pn = f.find(name='ProcessorNode')
            for t, (hh, rr) in enumerate(((h0, r0), (h1, r1))):
                k = _kids_of(f, f.recs[slice(*f.block(pn[2 * i + t]))])
                assert k['Header'].ints()[0] == hh and k['DataNodeRefs'].ints() == rr, \
                    '%s proc %d: derived (%d,%s), file (%s,%s)' % (
                        name, 2 * i + t, hh, rr, k['Header'].ints()[0], k['DataNodeRefs'].ints())
        vg = [r.ints() for r in f.find(name='VariantGroup')]
        assert vg == p['variant'], '%s variant: derived %s, file %s' % (name, p['variant'], vg)
        print('%-12s n=%d  reproduced exactly' % (name, len(nodes)))
    print('rule reproduces both shipped graphs')


if __name__ == '__main__':
    if '--selftest' in sys.argv:
        _selftest()
    else:
        n = int(sys.argv[1]) if len(sys.argv) > 1 else 3
        for k, v in plan(n, ['node%d' % i for i in range(n)]).items():
            print('%-24s %s' % (k, v))
