"""Zone records (gamedb/core/zonelist.lua): effective fields through _Inherits_, the menu, opening a shipped zone,
adding a new one. Replaces zone-info.ps1 / show-zone.ps1 / add-zone.ps1 (same edits, byte for byte).

Records are the entries of Records.{GlobalData, InheritList, PlaysetProperties, Zones}, one namespace (a zone's
_Inherits_ names an InheritList entry); a child's own fields win over its parent's. The zone name lowercased is
the world folder: PSX_Arcade_01 loads assets/worlds/psx_arcade_01/.
The menu gate is THREE flags, evaluated after _Inherits_ resolution: _ShowInMenu_, availableInConsumerBuild and
availableInShippingBuild. A parent may supply some and not others (Inherit_PSX_Arcade_Parent has no
availableInShippingBuild, so its zones stay hidden in the retail build whatever the child sets). Real zones inherit
a parent carrying MasterZone/PlaysetProperties/System/SaveZoneOverride; a record without one gets no environment
set up, which is why every world loaded through such a record -- stock 3.0 geometry included -- rendered unlit.
Start points can come from another zone: PSX_L1_Planet_Main has AlternateStartPointZone = "PSX_L1_Planet_Details".

The list is read from --zonelist FILE, else the install's loose assets/gamedb/core/zonelist.lua (startup2.zip
disabled and unpacked), else straight out of assets/startup2.zip. Edits go back to the same place; inside
startup2.zip through di3arc.add_files (the original kept once as startup2.zip.orig) -- so the archive no longer
has to be disabled (untested in-game; dna.zip edits made the same way load).

python zones.py info <zone>                       effective fields, own or inherited
python zones.py menu [--filter PlaysetX]          every zone the menu gate admits
python zones.py show <zone> [--revert]            add the menu flags a shipped record lacks
python zones.py add <zone> [--inherits PARENT] [--revert]   a new record, placed before PSX_Intro
  (all take --zonelist FILE or --game DIR)
"""
import argparse, re, sys
from pathlib import Path
import paths
from vtxf import lua_table

MEMBER = 'gamedb/core/zonelist.lua'
FLAGS = ('_ShowInMenu_', 'availableInConsumerBuild', 'availableInShippingBuild')
T = '\t'


def source(zonelist=None):
    """-> (zonelist.lua text, save(text), where it came from)"""
    if zonelist or paths.game('assets', *MEMBER.split('/')).exists():
        f = Path(zonelist) if zonelist else paths.game('assets', *MEMBER.split('/'))
        return (f.read_bytes().decode('utf-8'), lambda t: f.write_bytes(t.encode('utf-8')), str(f))
    import di3arc
    arc = paths.game('assets', 'startup2.zip')

    def save(text):
        orig = arc.with_name(arc.name + '.orig')
        if not orig.exists():
            orig.write_bytes(arc.read_bytes())
        arc.write_bytes(di3arc.add_files(arc, {MEMBER: text.encode('utf-8')}))
    return di3arc.unpack_index(arc)[MEMBER].decode('utf-8'), save, '%s:%s' % (arc, MEMBER)


def records(text):
    """{name: own fields} over every record group, in file order."""
    out = {}
    for group in lua_table(text)['Records'].values():
        if isinstance(group, dict):
            out.update(group)
    return out


def effective(recs, name, seen=()):
    own = recs.get(name)
    if not isinstance(own, dict) or name in seen:
        return {}
    out = dict(effective(recs, own['_Inherits_'], seen + (name,))) if '_Inherits_' in own else {}
    out.update(own)
    return out


def visible(e):
    return all(e.get(f) is True for f in FLAGS)


def _record_span(lines, zone, exact=False):
    """(first line, line of its closing '},') of a record, or None. exact: the close at the record's own depth
    (3 tabs), as add-zone.ps1 removed it; else the first '},' at any depth, as show-zone.ps1 did."""
    head = re.compile(r'^\t{3}%s = \s*$' % re.escape(zone))
    start = next((i for i, l in enumerate(lines) if head.match(l)), None)
    if start is None:
        return None
    close = (lambda l: re.match(r'^\t{3}\},\s*$', l)) if exact else (lambda l: l.strip() == '},')
    return start, next(i for i in range(start + 1, len(lines)) if close(lines[i]))


def show(text, zone, revert=False):
    """Insert the menu flags the record lacks (after its '{'), or remove them."""
    lines = text.split('\n')
    span = _record_span(lines, zone)
    if not span:
        raise SystemExit('%s not found in zonelist.lua' % zone)
    start, end = span
    flag = lambda l: next((f for f in FLAGS if re.match(r'^\t{4}%s = ' % f, l)), None)
    if revert:
        keep = [l for i, l in enumerate(lines) if not (start < i < end and flag(l))]
        return '\n'.join(keep), '%s: removed %d menu flag(s)' % (zone, len(lines) - len(keep))
    have = {flag(l) for l in lines[start:end]}
    added = [f for f in reversed(FLAGS) if f not in have]
    for f in added:
        lines.insert(start + 2, '%s%s = true,' % (T * 4, f))
    return '\n'.join(lines), ('%s: added %s -> assets/worlds/%s/' % (zone, ', '.join(added), zone.lower()) if added
                              else '%s already carried all three flags in its own record' % zone)


def add(text, zone, inherits=None, revert=False):
    """A new record before PSX_Intro (MasterZone PSX_Master + PlaysetX, or a parent to inherit), or remove it."""
    lines = text.split('\n')
    span = _record_span(lines, zone, exact=True)
    if revert:
        if not span:
            raise SystemExit('%s not present' % zone)
        return '\n'.join(lines[:span[0]] + lines[span[1] + 1:]), 'removed zone %s' % zone
    if span:
        raise SystemExit('%s already present' % zone)
    rec = ['%s%s = ' % (T * 3, zone), T * 3 + '{']
    if not inherits:
        rec += [T * 4 + 'MasterZone = "PSX_Master",', T * 4 + 'PlaysetProperties = "PlaysetX",']
    rec += [T * 4 + 'SplitScreenAllowed = false,'] + ['%s%s = true,' % (T * 4, f) for f in FLAGS]
    if inherits:
        rec.append('%s_Inherits_ = "%s",' % (T * 4, inherits))
    rec.append(T * 3 + '},')
    at = next((i for i, l in enumerate(lines) if re.match(r'^\t{3}PSX_Intro = \s*$', l)), None)
    if at is None:
        raise SystemExit('PSX_Intro anchor not found')
    return '\n'.join(lines[:at] + rec + lines[at:]), 'added zone %s -> assets/worlds/%s/' % (zone, zone.lower())


def main():
    ap = argparse.ArgumentParser(description='Zone records in zonelist.lua')
    ap.add_argument('cmd', choices=('info', 'menu', 'show', 'add'))
    ap.add_argument('zone', nargs='?')
    ap.add_argument('--filter', help='menu: only this PlaysetProperties')
    ap.add_argument('--inherits', help='add: parent record (e.g. an InheritList entry or a shipped zone)')
    ap.add_argument('--revert', action='store_true')
    ap.add_argument('--zonelist', help='zonelist.lua to read/edit (default: the install, loose or in startup2.zip)')
    paths.add_args(ap, 'game')
    a = paths.use(ap.parse_args())
    if a.cmd != 'menu' and not a.zone:
        ap.error('%s needs a zone' % a.cmd)
    text, save, where = source(a.zonelist)
    if a.cmd == 'info':
        recs = records(text)
        if a.zone not in recs:
            raise SystemExit('%s not found' % a.zone)
        e = effective(recs, a.zone)
        print('== %s  (own fields: %d, effective: %d)  [%s]' % (a.zone, len(recs[a.zone]), len(e), where))
        for k in sorted(e, key=str.lower):
            v = e[k]
            v = '{%d}' % len(v) if isinstance(v, (dict, list)) else str(v).lower() if isinstance(v, bool) else v
            print('  %-30s = %-28s [%s]' % (k, v, 'own' if k in recs[a.zone] else 'inherited'))
        print('  --> menu-visible: %s' % visible(e))
    elif a.cmd == 'menu':
        recs, n = records(text), 0
        for z in recs:
            e = effective(recs, z)
            if z.startswith('Inherit_') or not visible(e) or (a.filter and e.get('PlaysetProperties') != a.filter):
                continue
            n += 1
            print('%-34s playset=%-12s master=%s' % (z, e.get('PlaysetProperties', ''), e.get('MasterZone', '')))
        print('-- %d menu-visible zones' % n)
    else:
        new, msg = show(text, a.zone, a.revert) if a.cmd == 'show' else add(text, a.zone, a.inherits, a.revert)
        save(new)
        print(msg, '[%s]' % where)


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    main()
