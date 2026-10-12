"""Compare two worlds (folders) or two files, field by field: what a build changed against another build, a
converted world against a shipped one, or today's conversion against the one that worked in-game.

  tuple files (.oct/.mer/...)  every record gets a path of labels, a label being the record's name plus its
      identity -- the value of its NodeName / MaterialName / Name / RigName / GraphFileName / FileName child
      (an Association: Type:NodeName), else its ordinal among same-named siblings -- so one inserted node does not
      shift every later one. Reported per pool: records only in A, only in B, and changed values (floats equal
      within --tol); plus the header record count, the pool order and the name table size.
  .mtb      materials by GUID: shader, lookup hash, block A, texture slots (as texture hashes), block B rest
  other     size and equality (.vbuf/.ibuf/.btd/.btm/.ap/.mtc, or anything else)
Files pair up by suffix within the two folders (world names may differ: in_city vs out_intro2).

python diffworld.py <A dir|file> <B dir|file> [--all] [--tol 1e-5] [--pool SceneTreeNodePool] [--ext30 DIR]
  (.mtb materials are compared field by field when the 3.0 extract is set, else byte for byte)
"""
import argparse
from collections import Counter, defaultdict
from pathlib import Path
from oct import Oct, is_tuple_file, spec, NODE
import paths

IDENT = ('NodeName', 'MaterialName', 'Name', 'RigName', 'GraphFileName', 'FileName')


def addresses(f):
    """{address: record} for every record of an Oct; an address is the '/'-joined labels from the top."""
    out, stack, seen = {}, [], defaultdict(Counter)
    for i, r in enumerate(f.recs):
        del stack[r.level:]
        parent = '/'.join(stack)
        ident = None
        if r.level > 1 and i + 1 < len(f.recs) and f.recs[i + 1].level > r.level:   # pools keep their bare name
            k = f.kids(r, deep=False)
            if 'Type' in k and 'NodeName' in k and r.name == 'Association':
                ident = '%s:%s' % (k['Type'].value()[0], k['NodeName'].value()[0])
            else:
                ident = next((str(k[n].value()[0] if isinstance(k[n].value(), list) else k[n].value())
                              for n in IDENT if n in k), None)
        seen[parent][(r.name, ident)] += 1
        n = seen[parent][(r.name, ident)]
        label = (r.name or '(root)') + ('[%s]' % ident if ident is not None else '') + ('#%d' % n if n > 1 else '')
        stack.append(label)
        out['/'.join(stack)] = r
    return out


def _same(a, b, tol):
    if isinstance(a, list) and isinstance(b, list):
        return len(a) == len(b) and all(_same(x, y, tol) for x, y in zip(a, b))
    if isinstance(a, float) and isinstance(b, float):
        return abs(a - b) <= tol * max(1.0, abs(a), abs(b))
    return a == b


def _show(v, n=8):
    if isinstance(v, (bytes, bytearray)):
        return '<%d B>' % len(v)
    if isinstance(v, list) and len(v) > n:
        return '[%s, ... %d values]' % (', '.join(_show(x) for x in v[:n]), len(v))
    if isinstance(v, float):
        return '%.6g' % v
    if isinstance(v, list):
        return '[%s]' % ', '.join(_show(x) for x in v)
    return repr(v)


def diff_tuple(pa, pb, tol=1e-5, pool=None):
    """-> (summary lines, detail lines)"""
    a, b = Oct.load(pa), Oct.load(pb)
    A, B = addresses(a), addresses(b)
    head = []
    if a.f14 != b.f14:
        head.append('header record count %d -> %d' % (a.f14, b.f14))
    pa_, pb_ = [r.name for r in a.recs if r.level == 1], [r.name for r in b.recs if r.level == 1]
    if pa_ != pb_:
        head.append('pool order %s -> %s' % (pa_, pb_))
    if len(a.names) != len(b.names):
        head.append('name table %d -> %d names' % (len(a.names), len(b.names)))
    per, detail = defaultdict(Counter), []
    for k in sorted(set(A) | set(B)):
        top = (k.split('/') + [''])[1] or k          # the pool (a file's records hang under one root)
        if pool and top != pool:
            continue
        ra, rb = A.get(k), B.get(k)
        if ra is None or rb is None:
            # report only the top of a removed/added subtree
            parent = k.rsplit('/', 1)[0]
            if (ra is None and parent in B and parent not in A) or (rb is None and parent in A and parent not in B):
                continue
            per[top]['only in B' if ra is None else 'only in A'] += 1
            detail.append(('+ ' if ra is None else '- ') + k)
            continue
        if spec(ra.type)[0] == NODE and spec(rb.type)[0] == NODE:     # a node's key is its index label
            continue
        va, vb = ra.value(), rb.value()
        if ra.type != rb.type and not _same(va, vb, tol):
            per[top]['changed'] += 1
            detail.append('~ %s: %s -> %s  (type %#x -> %#x)' % (k, _show(va), _show(vb), ra.type, rb.type))
        elif not _same(va, vb, tol):
            per[top]['changed'] += 1
            if isinstance(va, list) and isinstance(vb, list) and len(va) == len(vb):
                d = [i for i, (x, y) in enumerate(zip(va, vb)) if not _same(x, y, tol)]
                detail.append('~ %s: %d of %d values differ, first [%d] %s -> %s'
                              % (k, len(d), len(va), d[0], _show(va[d[0]]), _show(vb[d[0]])))
            else:
                detail.append('~ %s: %s -> %s' % (k, _show(va), _show(vb)))
    summary = head + ['%-28s %s' % (p, ', '.join('%s %d' % kv for kv in sorted(c.items()))) for p, c in sorted(per.items())]
    return summary, detail


def diff_mtb(pa, pb):
    import matp

    def mats(p):
        bnd = matp.Bundle(Path(p).read_bytes())
        out = {}
        for m in bnd.materials:
            slots = bnd.tex_slots(m)
            out[m['name']] = dict(shader=m['shader'], hash=m['hash'], A=m['A'],
                                  textures=[bnd.textures[t].hex() if t != 0xffffffff else None for t in slots],
                                  Brest=m['B'][m['stride'] * m['slots']:])
        return out
    a, b = mats(pa), mats(pb)
    summary, detail = [], []
    for g in sorted(set(a) | set(b)):
        if g not in b or g not in a:
            detail.append(('- ' if g not in b else '+ ') + 'material ' + g)
            continue
        ch = [f for f in a[g] if a[g][f] != b[g][f]]
        if ch:
            detail.append('~ material %s: %s' % (g, ', '.join(
                '%s %s -> %s' % (f, _show(a[g][f]), _show(b[g][f])) if f in ('shader', 'hash', 'textures') else f
                for f in ch)))
    if not detail:
        return [], []
    c = Counter(d[0] for d in detail)
    summary.append('materials %d -> %d: %d only in A, %d only in B, %d changed'
                   % (len(a), len(b), c['-'], c['+'], c['~']))
    return summary, detail


def diff_file(pa, pb, tol=1e-5, pool=None):
    pa, pb = Path(pa), Path(pb)
    if pa.suffix == '.mtb' and paths.where('ext30')[0]:      # materials need the 3.0 shaders to size their blocks
        return diff_mtb(pa, pb)
    if is_tuple_file(pa) and is_tuple_file(pb):
        return diff_tuple(pa, pb, tol, pool)
    x, y = pa.read_bytes(), pb.read_bytes()
    if x == y:
        return [], []
    return ['%d B -> %d B, contents differ' % (len(x), len(y))], []


def pairs(a, b):
    """Files of folders a and b paired by name with the world name (its .oct's stem, else the folder name)
    stripped: in_city_0.vbuf and out_intro2_0.vbuf -> '_0.vbuf'."""
    def files(d):
        d = Path(d)
        world = next((p.stem for p in d.glob('*.oct')), d.name).lower()
        return {(p.name[len(world):] if p.name.lower().startswith(world) else p.name): p
                for p in d.iterdir() if p.is_file()}
    A, B = files(a), files(b)
    return [(A.get(k), B.get(k)) for k in sorted(set(A) | set(B))]


def main():
    ap = argparse.ArgumentParser(description='Compare two worlds or files field by field')
    ap.add_argument('a')
    ap.add_argument('b')
    ap.add_argument('--all', action='store_true', help='every detail line (default: the first 40 per file)')
    ap.add_argument('--tol', type=float, default=1e-5, help='relative float tolerance')
    ap.add_argument('--pool', help='tuple files: only this top-level pool')
    paths.add_args(ap, 'ext30')
    a = paths.use(ap.parse_args())
    if Path(a.a).is_dir() != Path(a.b).is_dir():
        ap.error('compare two folders or two files')
    todo = pairs(a.a, a.b) if Path(a.a).is_dir() else [(Path(a.a), Path(a.b))]
    same = 0
    for x, y in todo:
        if x is None or y is None:
            print('%s only in %s' % ((x or y).name, 'A' if y is None else 'B'))
            continue
        summary, detail = diff_file(x, y, a.tol, a.pool)
        if not summary and not detail:
            same += 1
            continue
        print('== %s  vs  %s' % (x.name, y.name))
        for s in summary:
            print('   ' + s)
        for d in (detail if a.all else detail[:40]):
            print('     ' + d)
        if len(detail) > 40 and not a.all:
            print('     ... %d more (--all)' % (len(detail) - 40))
    print('%d of %d file pairs identical' % (same, len(todo)))


if __name__ == '__main__':
    main()
