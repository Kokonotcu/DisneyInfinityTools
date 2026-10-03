"""3.0 SubNetwork count fields, derived from a SubNetwork's own content (fitting stage).

3.0 adds per-SubNetwork counts that 1.0 graphs lack (ConstantHeaderCount ... BaseDataNodeLinksCount,
ConstantLayoutCount). This module measures candidate content statistics on every shipped 3.0
SubNetwork so each count can be stated as a rule and checked as a prediction over the corpus.

python subnet.py --fit      print, per count field, how often each candidate statistic matches
"""
import sys
from collections import Counter, defaultdict
from pathlib import Path
from oct import Oct

COUNTS = ('ConstantHeaderCount', 'VariableHeaderCount', 'ConstantDataNodeCount', 'VariableDataNodeCount',
          'ProcessorNodeCount', 'BasisCount', 'BasisConversionCount', 'ComponentFamilyCount',
          'ComponentMemberCount', 'VariantGroupCount', 'VariantMemberCount', 'LayerBindCount',
          'BaseDataNodeLinksCount', 'ConstantLayoutCount')


def subnets(s):
    """[(subnetwork record, {level-3 field: rec}, [records below])]"""
    pool = next((r for r in s.recs if r.level == 1 and r.name == 'SubNetworkPool'), None)
    if pool is None:
        return []
    out = []
    for sn in [r for r in s.recs[s.block(pool)[0]:s.block(pool)[1]] if r.level == 2]:
        i, j = s.block(sn)
        body = s.recs[i + 1:j]
        out.append((sn, {r.name: r for r in body if r.level == 3}, body))
    return out


def stats(body):
    """Candidate content statistics of one SubNetwork."""
    st = Counter()
    pool = None
    for r in body:
        if r.level == 3:
            pool = r.name
        elif r.level == 4:
            st['n4:%s/%s' % (pool, r.name)] += 1
        elif r.level == 5:
            st['n5:%s/%s' % (pool, r.name)] += 1
            if r.name == 'External':
                try:
                    st['ext%d:%s' % (r.ints()[0], pool)] += 1
                except Exception:
                    pass
    for r in body:
        if r.level == 3 and r.name in ('HeaderStrings', 'HeaderStringIndices', 'HeaderLayout'):
            try:
                v = r.value()
                st['len:' + r.name] = len(v)
            except Exception:
                pass
    return st


def _one(p):
    try:
        s = Oct.load(str(p))
    except Exception:
        return []
    rows = []
    for sn, f, body in subnets(s):
        if 'VariableHeaderCount' not in f:
            continue
        got = {}
        for c in COUNTS:
            if c in f:
                try:
                    got[c] = f[c].ints()[0]
                except Exception:
                    pass
        rows.append((got, dict(stats(body))))
    return rows


def fit():
    from par import pmap
    ps = sorted(Path(r'D:\DI-Extract\3.0\assets\worlds').glob('*/*.oct'))
    rows = [r for rs in pmap(_one, ps, chunksize=8) for r in rs]
    print('shipped SubNetworks', len(rows))
    for c in COUNTS:
        hit = Counter()
        n = 0
        for got, st in rows:
            if c not in got:
                continue
            n += 1
            for k, v in st.items():
                if v == got[c]:
                    hit[k] += 1
            if got[c] == 0:
                hit['ZERO'] += 1
        best = hit.most_common(4)
        print('%-24s n=%-6d %s' % (c, n, ', '.join('%s %d' % kv for kv in best)))


if __name__ == '__main__':
    if sys.argv[1:] == ['--fit']:
        fit()
