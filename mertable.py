"""Meridian class table for di1compat's .mer transcode (mer.hpp): 3.0-side knowledge only.
  R <class>            a node class the 3.0 exe registers (DisneyInfinity3.exe registration strings)
  C <class>            a class seen in shipped 3.0 graphs, followed by
  P <name>             a pin of it (instances list pins by name; the index is the file's own)
  A <name> <type>      a parameter and the type 3.0 reads it as: bool int float string vec3
Sources: mer.py catalogue over the 3.0 extract (pins and parameter types of 777 classes in 584 graphs) and the
registered-class list from the exe (research scratch regclasses_30.json, 1,063 names). Classes no shipped 3.0 graph
uses fall back to the 1.0 pin tables (pins10_all.json), keeping only pins that carry links there (that table lists
some parameters as pins; an unlinked pin read as a parameter is harmless).
python mertable.py <catalogue.json> <regclasses_30.json> <pins10_all.json> <out.txt>"""
import json, sys


def main(cat_path, reg_path, pins10_path, out_path):
    cat = json.load(open(cat_path))['types']
    reg = json.load(open(reg_path))
    p10 = json.load(open(pins10_path))
    lines = ['R ' + c for c in sorted(reg) if c and '%' not in c and ' ' not in c]
    for c in sorted(set(p10) - set(cat)):
        linked = set(p10[c].get('link_pins_in', [])) | set(p10[c].get('link_pins_out', []))
        pins = [n for n, i in p10[c].get('pins', {}).items() if i in linked and ' ' not in n]
        if pins and ' ' not in c:
            lines += ['C ' + c] + ['P ' + n for n in sorted(pins)]
    for c in sorted(cat):
        t = cat[c]
        if ' ' in c:
            continue
        lines.append('C ' + c)
        pins = set()
        for p in t.get('pins', {}).values():   # a name, or {name: count} where instances disagree on an index
            pins.update([p] if isinstance(p, str) else p.keys())
        lines += ['P ' + p for p in sorted(pins) if ' ' not in p]
        lines += ['A %s %s' % (n, v['type']) for n, v in sorted(t.get('params', {}).items())
                  if ' ' not in n and v.get('type') in ('bool', 'int', 'float', 'string', 'vec3')]
    open(out_path, 'w', newline='\n').write('\n'.join(lines) + '\n')
    print('%d registered, %d classes, %d lines -> %s' % (sum(l[0] == 'R' for l in lines), sum(l[0] == 'C' for l in lines),
                                                          len(lines), out_path))


if __name__ == '__main__':
    main(*sys.argv[1:5])
