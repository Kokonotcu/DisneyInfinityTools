"""Meridian class table for di1compat's .mer transcode (mer.hpp): 3.0-side knowledge only.
  R <class>            a node class the 3.0 exe registers (DisneyInfinity3.exe registration strings)
  C <class>            a class seen in shipped 3.0 graphs, followed by
  P <name>             a pin of it (instances list pins by name; the index is the file's own)
  A <name> <type>      a parameter and the type 3.0 reads it as: bool int float string vec3
Sources: mer.py catalogue over the 3.0 extract (pins and parameter types of 777 classes in 584 graphs) and the
registered-class list from the exe (research scratch regclasses_30.json, 1,063 names). Classes no shipped 3.0 graph
uses fall back to the 1.0 pin tables (pins10_all.json), keeping only pins that carry links there (that table lists
some parameters as pins; an unlinked pin read as a parameter is harmless).
Optional 5th argument (defs30.json, from the exe): parameter types of registered classes no shipped 3.0 graph
describes. 3.0's getter uses a value only when its description type equals the getter's (FUN_006a9740), and 1.0
stores every bool as an int record, so without a type such values fell back to defaults (paramtype.py over the 598
Incredibles graphs: 729 values, 38 non-zero -- MissionFocusPoint_Rule_ActorState CheckStateNotEqual,
MissionTask_Customization_Blockhead TestSpecificActors, OmnidroidParms bools, ...). A type is taken from
  - the classes registered by the same helper (regclasses_30.json: MissionFocusPoint_Actor and _ActorTask share
    FUN_0177baa0, so they are one node class under two names) when every one of them that describes the name
    agrees, else
  - the exe getter the node calls with the name (defs30.json 'params' under the class's own name, else under the
    node RTTI of its definition when no other registered class uses that definition: classdefs_30.json).
Without them the output is byte-identical to the 4-argument table.
python mertable.py <catalogue.json> <regclasses_30.json> <pins10_all.json> <out.txt> [defs30.json classdefs_30.json]"""
import argparse, json
from collections import Counter, defaultdict

TYPES = ('bool', 'int', 'float', 'string', 'vec3')


def borrowed_types(cat, reg, d30, cdefs):
    """class -> {param: type} it lacks, for registered classes: same-helper twins (unanimous), then exe getters"""
    helper = defaultdict(set)
    for c, v in reg.items():
        if v and v[0] != 'direct':
            helper[v[0]].add(c)
    getters = {}
    for k, v in d30.items():
        for t, n, *x in (v.get('params') or []):
            if t in TYPES:
                getters.setdefault(k, {}).setdefault(n, t)
    users = Counter(d for d in cdefs.values() if d)
    pins = defaultdict(set)   # a data_in+param pin (FUN_006c7f80) is a pin first: mer.hpp copies pin tables by it
    for v in d30.values():
        if v.get('def'):
            pins[v['def']].update(n for b, n in v.get('pins') or [])
    out = {}
    for c, v in reg.items():
        if not v or ' ' in c or '%' in c:
            continue
        own = cat.get(c, {}).get('params', {})
        seen = defaultdict(set)
        for t in helper.get(v[0], ()):
            for n, p in cat.get(t, {}).get('params', {}).items():
                seen[n].add(p.get('type'))
        add = {n: next(iter(s)) for n, s in seen.items() if len(s) == 1 and next(iter(s)) in TYPES}
        d = cdefs.get(c)
        g = getters.get(c) or (getters.get(d.replace('Definition', ''), {}) if d and users[d] == 1 else {})
        for n, t in g.items():
            add.setdefault(n, t)
        add = {n: t for n, t in add.items() if n not in own and n not in pins[d] and ' ' not in n}
        if add:
            out[c] = add
    return out


def main(cat_path, reg_path, pins10_path, out_path, d30_path=None, cdefs_path=None):
    cat = json.load(open(cat_path))['types']
    reg = json.load(open(reg_path))
    p10 = json.load(open(pins10_path))
    extra = borrowed_types(cat, reg, json.load(open(d30_path)), json.load(open(cdefs_path))) if cdefs_path else {}
    lines = ['R ' + c for c in sorted(reg) if c and '%' not in c and ' ' not in c]
    for c in sorted(set(p10) - set(cat)):
        linked = set(p10[c].get('link_pins_in', [])) | set(p10[c].get('link_pins_out', []))
        pins = [n for n, i in p10[c].get('pins', {}).items() if i in linked and ' ' not in n]
        if pins and ' ' not in c:
            lines += ['C ' + c] + ['P ' + n for n in sorted(pins)]
            lines += ['A %s %s' % (n, t) for n, t in sorted(extra.pop(c, {}).items())]
    for c in sorted(cat):
        t = cat[c]
        if ' ' in c:
            continue
        lines.append('C ' + c)
        pins = set()
        for p in t.get('pins', {}).values():   # a name, or {name: count} where instances disagree on an index
            pins.update([p] if isinstance(p, str) else p.keys())
        lines += ['P ' + p for p in sorted(pins) if ' ' not in p]
        own = {n: v['type'] for n, v in t.get('params', {}).items() if ' ' not in n and v.get('type') in TYPES}
        own.update(extra.pop(c, {}))
        lines += ['A %s %s' % (n, ty) for n, ty in sorted(own.items())]
    for c in sorted(extra):   # registered, in no 3.0 graph and no 1.0 pin table: parameter types only
        lines += ['C ' + c] + ['A %s %s' % (n, t) for n, t in sorted(extra[c].items())]
    open(out_path, 'w', newline='\n').write('\n'.join(lines) + '\n')
    print('%d registered, %d classes, %d lines -> %s' % (sum(l[0] == 'R' for l in lines), sum(l[0] == 'C' for l in lines),
                                                          len(lines), out_path))


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    for n in ('catalogue', 'regclasses', 'pins10', 'out'):
        ap.add_argument(n)
    ap.add_argument('defs30', nargs='?')
    ap.add_argument('classdefs', nargs='?')
    a = ap.parse_args()
    main(a.catalogue, a.regclasses, a.pins10, a.out, a.defs30, a.classdefs)
