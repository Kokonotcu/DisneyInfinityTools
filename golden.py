"""Golden baseline for the native plugin: octtest's outputs and the plugin's source facts, recorded once and checked
after every refactor step (docs/audit X-13, OT-27).

python golden.py record <label> [case-glob ...] --golden DIR --octtest EXE --inst10 DIR --ext10 DIR --ext30 DIR
python golden.py check [case-glob ...] [--baseline LABEL] [--golden DIR] [--octtest EXE] [--quick]
python golden.py list [case-glob ...] [--quick]

record runs every case and stores, per case, the exit code, stdout and stderr (normalised) and every file it wrote,
with their sha256, under <golden>/<label>/. The data roots are stored with it, so check needs none of them again.
check re-runs the cases into <golden>/_check/<run>/ and exits 1 on any difference, with a short diff per failing
case; a passing run's outputs are deleted. Case globs pick cases by name ('static/*' needs no octtest); give them
together. --quick runs the fast subset. --golden defaults to $DI_GOLDEN, --octtest to build/Release/octtest.exe,
--baseline to the newest one.

Normalisation (OT-22): data roots and the case's own folders in printed paths become {ext10} {ext30} {game} {inst10}
{inputs} {out} {in}; timings and other run-to-run noise are masked per case; lines whose order is not part of the
output (unordered containers) are sorted. Written files are compared byte for byte.
<golden>/inputs holds fixed inputs (tables, twin pairs, name lists, gen30.py's dump); never edit them.
The static cases need no octtest: every log format string passed to logf (multiset), the hook rows per feature in
install order, and the config keys with their kinds and defaults, read from native/di1compat's sources.
"""
import argparse, difflib, fnmatch, glob, hashlib, json, os, re, shutil, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import paths

HERE = Path(__file__).resolve().parent
SRC = HERE / 'native' / 'di1compat'
MS = (r' *\d+(\.\d+)? ms', ' # ms')
ROOTS = ('game', 'ext10', 'ext30', 'inst10', 'inputs')


class Case:
    """One octtest run. args/env use {placeholders}; ('glob', pattern) expands to the sorted matches, ('tmpl', file, n)
    to a copy of file (its first n lines if n) with the placeholders filled. Output paths go under {out}.
    mask: (regex, replacement) per line; drop: lines removed; sort / sort_err: regex of the stdout lines sorted among
    themselves, or True for all lines."""

    def __init__(self, name, args, env=None, quick=False, mask=(), drop=(), sort=None, serial=False, sort_err=None):
        self.name, self.args, self.env, self.quick = name, args, env or {}, quick
        self.mask, self.drop, self.sort, self.serial, self.sort_err = mask, drop, sort, serial, sort_err


def cases():
    T, M = '{inputs}/di1compat_render.txt', '{inputs}/di1compat_mer.txt'
    E10, E30 = '{ext10}/assets', '{ext30}/assets'
    W = E10 + '/worlds/in_city/in_city'
    BOB = E10 + '/characters/inc_bob/inc_bob/inc_bob'
    CHO = E10 + '/choreographies/in/in_city_boss_location/in_city_boss_location.oct'
    REALM = E10 + '/realms/daygeneric_realm/daygeneric_realm.oct'
    DM10, CORE10 = E10 + '/gamedb/core/core/incrediblesdatamap.lua', E10 + '/startup2/gamedb/core/coredatamap.lua'
    TB30, DORY30 = E30 + '/gamedb/core/core/toyboxdatamap.lua', E30 + '/gamedb/core/core/dorydatamap.lua'
    ZL10, ZL30 = E10 + '/startup2/gamedb/core/zonelist.lua', E30 + '/startup2/gamedb/core/zonelist.lua'
    SURF30 = E30 + '/startup2/gamedb/core/surfacetypes.lua'
    PKGS = ('glob', E10 + '/expendables_data/expendables_data/*.lua')
    PKGS_CORE = ('glob', E10 + '/startup3/expendables_data/*.lua')
    STORY = E10 + '/mission_data/incredibles/in_city_story/in_city_story/'
    ACT = E10 + '/mission_data/incredibles/in_city_activity/in_city_activity/'
    SLACK = {'OCTX_SLACK': '4096'}   # the plugin's arena slack (octx::g_slack, CO-01 / OT-08)
    DUMP = {'OCTTEST_DUMP': '{out}/first-mismatch.oct'}   # --counts writes the first regenerated file that differs
    PLUGIN = {'RENDER_TRANSLATE': '1', 'RENDER_SKIN': '1', 'RENDER_SSF': '1', 'RENDER_SKINTR': '1',
              'RENDER_VERBOSE': '1', 'RENDER_GENCHECK': '{out}/dxbc'}   # the recommended config (OT-11, OT-18)
    FEVS = ('IN_City blockhead_hulk Omnidroid_RangedI Omnidroid_MeleeI Inc_Syndrome _townspeople '
            'Inc_MrInc_SportsCar_Mini Inc_Bob TrafficCar_Exploding Inc_Townspeople INC_Docks steps Inc_OmniSky_Spawner '
            'Omnidroid_Tank Zurgbot_Melee Intro MU_Randall MU_Mike WR_Vanellope NBC_JackSkellington WR_Ralph '
            'TAN_Rapunzel PC_Cannons Edna').split()
    c = [
        # codec acceptance and counts (the round trip, OCT-1; --counts also at the plugin's slack)
        Case('roundtrip/in_city', [E10 + '/worlds/in_city'], quick=True),
        Case('roundtrip/all', [E10, E30]),
        Case('counts/3.0', ['--counts', E30], DUMP),
        Case('counts/3.0-slack4096', ['--counts', E30], dict(SLACK, **DUMP)),
        Case('counts/worlds-a', ['--counts', ('glob', E30 + '/worlds/a*')], DUMP, quick=True),
        Case('counts/worlds-a-slack4096', ['--counts', ('glob', E30 + '/worlds/a*')], dict(SLACK, **DUMP), quick=True),
        Case('readable', ['--readable'], quick=True, serial=True, mask=[(r'[\d.]+ us', '# us')]),
        # .oct transforms (octx, gameplay, cho) as octtest strings them together
        Case('oct/in_city', ['--oct', W + '.oct', '{out}/in_city.oct'], quick=True, mask=[MS]),
        Case('oct/in_city-slack4096', ['--oct', W + '.oct', '{out}/in_city.oct'], SLACK, quick=True, mask=[MS]),
        Case('oct/cho', ['--oct', CHO, '{out}/cho.oct', 'cho'], quick=True, mask=[MS]),
        Case('oct/cho-slack4096', ['--oct', CHO, '{out}/cho.oct', 'cho'], SLACK, quick=True, mask=[MS]),
        Case('oct/realm', ['--oct', REALM, '{out}/realm.oct'], quick=True, mask=[MS]),
        Case('oct/realm-slack4096', ['--oct', REALM, '{out}/realm.oct'], SLACK, quick=True, mask=[MS]),
        Case('oct/bob', ['--oct', BOB + '.oct', '{out}/bob.oct', 'cho'], quick=True, mask=[MS]),
        Case('surf', ['--surf', SURF30, W + '.oct', ('glob', STORY + '*.oct')], quick=True),
        Case('nav/in_city', ['--nav', '{out}', W + '.oct'], quick=True),
        Case('nav/worlds', ['--nav', '{out}', ('glob', E10 + '/worlds/*/*.oct')]),
        Case('skin', ['--skin', E10 + '/shaders/shaders/vformat.vtxf', '{out}', BOB + '.oct',
                      E10 + '/characters/inc_bob/inc_bob_cin/inc_bob_cin.oct'], quick=True),
        Case('btd', ['--btd', W + '.btd', '{out}/in_city.btd'], quick=True),
        Case('bent/bob', ['--bent', BOB + '.bent', 'characters/inc_bob/inc_bob.bent', '{out}/inc_bob.bent'],
             quick=True),
        Case('bent/crate', ['--bent', E10 + '/characters/carriable/crate_small_nb/crate_small_nb.bent',
                            'characters/carriable/crate_small_nb.bent', '{out}/crate.bent'], quick=True),
        # choreography (cho.hpp, ports10.hpp), corpus checks and twins
        Case('cho/layout', ['--cho-layout', E10, E30]),
        Case('cho/time', ['--cho-time', E30, 'choreographies', 'exploders']),
        Case('cho/time10', ['--cho-time10', E10, E30]),
        Case('cho/anim', ['--cho-anim', E10, E30]),
        Case('cho/scene', ['--cho-scene', E10, E30]),
        Case('cho/const', ['--cho-const', E10, E30]),
        Case('cho/const-slack4096', ['--cho-const', E10, E30], SLACK),
        Case('cho/variants', ['--variants', E10, E30]),
        Case('cho/variants-off', ['--variants', E10, E30, '0']),
        Case('cho/variants-slack4096', ['--variants', E10, E30, '1'], SLACK),
        Case('cho/clips', ['--cho-clips', ('tmpl', '{inputs}/chopairs.tmpl', 0)]),
        Case('cho/clips-motions', ['--cho-clips', ('tmpl', '{inputs}/clippairs.tmpl', 0)]),
        Case('cho/clips-quick', ['--cho-clips', ('tmpl', '{inputs}/chopairs.tmpl', 40)], quick=True),
        Case('cho/clip-ids', ['--clip-ids', ('tmpl', '{inputs}/clippairs.tmpl', 0)]),
        Case('cho/clip-ids-quick', ['--clip-ids', ('tmpl', '{inputs}/clippairs.tmpl', 300)], quick=True),
        Case('cho/banm', ['--banm', ('tmpl', '{inputs}/banmpairs.tmpl', 0)]),
        Case('cho/clipdump', ['--cho-clipdump', E10 + '/choreographies/in/inc_cin_openinga/inc_cin_openinga.oct',
                              '{out}/blocks.bin'], quick=True),
        Case('cho/file', ['--cho-file', CHO, ('glob', E10 + '/choreographies/in/inc_cin_*/inc_cin_*.oct')],
             {'CHO_OUT': '{out}'}, quick=True),
        Case('cho/corpus', ['--cho-corpus', E10, 'choreographies', 'exploders']),
        Case('cho/alias', ['--cho-alias', DM10, 'choreographies/bolt_incbob/bolt_incbob.oct',
                           'choreographies/door/door_0.vbuf',
                           'choreographies/pnf_baseball_physics/pnf_baseball_physics.mtb',
                           'choreographies/door/doorx.oct',
                           'choreographies/in_city_boss_location/in_city_boss_location.oct', 'nothing/here.oct'],
             quick=True),
        # Meridian graphs: default table flags, then each cfg switch off and the registry aliases
        Case('mer/in_city', ['--mer', M, W + '.mer', '{out}/in_city.mer', '{out}/in_city.mtc'], quick=True),
        Case('mer/story', ['--mer', M, STORY + 'build_smprison.mer', '{out}/m.mer', '{out}/m.mtc'], quick=True),
    ]
    for flag in ('noportrefs', 'noports', 'nopins', 'noevents', 'nogate', 'notoggletype', 'aliases'):
        c.append(Case('mer/in_city-' + flag, ['--mer', M, W + '.mer', '{out}/in_city.mer', '{out}/in_city.mtc',
                                              '--' + flag], quick=True))
    c += [
        Case('mer/activity', ['--activity', M, '{out}', ('glob', ACT + '*.mer'), ('glob', ACT + '*.lua'),
                              ('glob', ACT + '*.ap')], quick=True),
        # missions, challenges, zones, gameplay
        Case('mission', ['--mission', STORY + 'build_smprison.lua', '{out}/m.lua'], quick=True),
        Case('mischain', ['--mischain', ('glob', STORY + '*.lua'), ('glob', ACT + '*.lua')], quick=True),
        Case('missiongraph/activity', ['--missiongraph', ACT + 'in_city_activity__missiongraph.lua', '{out}/mg.lua'],
             quick=True),
        Case('missiongraph/story', ['--missiongraph', STORY + 'in_city_story__missiongraph.lua', '{out}/mg.lua'],
             quick=True),
        Case('beaconmap', ['--beaconmap', ZL30, '{out}/zonelist.lua'], quick=True),
        Case('challenge', ['--challenge', '{out}', ('glob', E10 + '/activitydata/activitydata/activitytypedata*.lua'),
                           E10 + '/activitydata/activitydata/defaultactivitybeacons.lua',
                           E30 + '/challengedata/challengedata/challengedata.lua', ZL30, ('glob', ACT + '*.ap'),
                           ('glob', E10 + '/characters/characters/inc_*.dnax')], quick=True),
        Case('tools', ['--tools', E10 + '/startup5/var/toolset/toolset.bin', 'IN_City'], quick=True),
        Case('gpscan', ['--gpscan', SURF30, '{inst10}/assets']),
        Case('zones10', ['--zones10', '{inst10}', ZL30, '{out}/zonelist.lua'], quick=True),
        Case('zones10-noadd', ['--zones10', '{inst10}', ZL30, '{out}/zonelist.lua', 'noadd'], quick=True),
        Case('zones10-only', ['--zones10', '{inst10}', ZL30, '{out}/zonelist.lua', 'only'], quick=True),
        Case('menu', ['--menu', '{out}/menu.lua', '{inst10}', ZL30, '{game}/assets/presentation/presentation.zip',
                      'playsetlistwin32.lua'], quick=True),
        Case('currency10', ['--currency10', ZL10], quick=True),
        # GameDB, DataMaps, Lua chunks, scripts (luachunk, datamap, chd, sys, npc, sx)
        Case('datamap', ['--datamap', DM10, '{out}/dm.lua'], quick=True),
        Case('dbtriggers', ['--dbtriggers', DM10, CORE10], quick=True),
        Case('dmeval', ['--dmeval', DM10, TB30], quick=True),
        Case('gamedb', ['--gamedb', DM10, TB30], quick=True),
        Case('playset', ['--playset', DM10, TB30, DORY30, 'incredibles', CORE10], quick=True),
        Case('psdna', ['--psdna', E10 + '/characters/characters', E30 + '/characters/characters', '{out}'], quick=True),
        Case('avperc', ['--avperc', E10 + '/characters/characters/avatar.btx',
                        E30 + '/characters/characters/avatar.btx', 'IN_City'], quick=True),
        Case('groups30', ['--groups30', '{out}', PKGS, PKGS_CORE], quick=True),
        Case('groups30-dna', ['--groups30', '{out}', PKGS, PKGS_CORE], {'GROUPS30_DNA': E10}, quick=True),
        Case('keep', ['--keep', E10 + '/gamedb/incredibles/incredibles/locks__lua.lua',
                      E30 + '/startup2/gamedb/core/locks__lua.lua',
                      E30 + '/gamedb/incredibles/incredibles/locks__lua.lua'],
             {'KEEP_OUT': '{out}/keep.lua'}, quick=True),
        Case('chest', ['--chest', E10 + '/startup5/var/chests/chestinfo.bin', '{out}/chest.lua'], quick=True),
        Case('perc', ['--perc', E10 + '/startup5/var/perceptions/perceptioninfo.bin', '{inputs}/names30.txt',
                      '{inputs}/groups30.tsv', '{out}/perc.lua'], quick=True),
        Case('chd/incredibles', ['--chd', E30 + '/gamedb/incredibles'], quick=True),
        Case('chd/all', ['--chd', E30 + '/gamedb']),
        Case('dct', ['--dct', E10 + '/lang/in/in/english_ntsc_win32.dct', E10 + '/lang/in/in/stringids_win32.dct',
                     'INV_Inc_MrInc_SportsCar_Name'], quick=True),
        Case('dct-miss', ['--dct', E10 + '/lang/in/in/english_ntsc_win32.dct', E10 + '/lang/in/in/stringids_win32.dct',
                          'Load_Tips_Incredibles_01'], quick=True),
        Case('sxkeys', ['--sxkeys', '{inputs}/names30.txt', '{out}/keys.tsv'], quick=True),
        Case('ai', ['--ai', '{out}', E10 + '/characters/characters', E10 + '/scripts/scripts'], quick=True),
        Case('sx', ['--sx', '{out}', E10 + '/characters/characters', E10 + '/scripts/scripts'], quick=True),
        # UI
        Case('ui', ['--ui', '{ext10}', '{ext30}', '{out}']),
        Case('texname', ['--texname', E10 + '/flash/aboutdialog/aboutdialog/aboutdialog_i1.mtb', '{out}/x_i1.mtb'],
             quick=True),
        # render, shaders (gen30, sm3), vertex formats, archives (arc30, the 1.0 zips)
        Case('vtxf', ['--vtxf', '{ext30}', '{ext10}', '{out}/vformat.vtxf'], quick=True),
        Case('render/in_city', ['--render', E10 + '/worlds/in_city', T, '{ext30}', '{ext10}', '{out}'], mask=[MS],
             quick=True),
        Case('render/in_city-plugin', ['--render', E10 + '/worlds/in_city', T, '{ext30}', '{ext10}', '{out}'], PLUGIN,
             mask=[MS]),
        Case('render/in_city-arc30', ['--render', E10 + '/worlds/in_city', T, '{ext30}', '{ext10}', '{out}'],
             {'ARC30_GAME': '{game}'}, mask=[MS]),
        Case('render/bob', ['--render', E10 + '/characters/inc_bob/inc_bob', T, '{ext30}', '{ext10}', '{out}'],
             mask=[MS], quick=True),
        Case('render/bob-plugin', ['--render', E10 + '/characters/inc_bob/inc_bob', T, '{ext30}', '{ext10}', '{out}'],
             PLUGIN, mask=[MS]),
        Case('render/realm', ['--render', E10 + '/realms/daygeneric_realm', T, '{ext30}', '{ext10}', '{out}'],
             mask=[MS], quick=True),
        Case('mtb', ['--mtb', T, '{ext30}', '{ext10}', BOB + '.mtb', 'characters/inc_bob/inc_bob.mtb', '{out}/bob.mtb'],
             {'RENDER_VERBOSE': '1'}, quick=True),
        Case('gen30', ['--gen30', T, '{ext10}', '{inputs}/gen30py'], {'GEN30_DUMP': '{out}'}),
        Case('warm', ['--warm', T, '{ext10}', '{out}', W + '.mtb', BOB + '.mtb']),
        Case('warm-ext30', ['--warm', T, '{ext10}', '{out}', W + '.mtb', BOB + '.mtb'], {'WARM_EXT30': '{ext30}'}),
        Case('arc', ['--arc', T, '{game}', '{ext30}'], sort=r'MISMATCH ', sort_err=True),
        Case('member', ['--member', '{game}/assets/presentation/presentation.zip', 'playsetlistwin32.lua'], quick=True),
        Case('zip', ['--zip', '{inst10}/assets/worlds/in_city.zip', 'in_city.oct'], quick=True),
        Case('read10', ['--read10', '{inst10}', 'Choreographies/IN/in_city_boss_location/in_city_boss_location.oct'],
             quick=True),
        # particles (vfx, chan)
        Case('pd/in_city', ['--pd', E10 + '/particles/in_city/in_city.particledata', '{out}/in_city.particledata'],
             quick=True),
        Case('pd/in_city-chan', ['--pd', E10 + '/particles/in_city/in_city.particledata', '{out}/in_city.particledata',
                                 T, '{ext30}', '{ext10}', 'particles/in_city/in_city.particledata'], quick=True),
        Case('pd/p3d', ['--pd', E10 + '/particles/in_city/particles/asph_a.p3d', '{out}/asph_a.p3d'], quick=True),
        Case('vfxchan', ['--vfxchan', '{ext10}', '{ext30}']),
        # audio: the FMOD host on the real 1.0 DLLs, no sound device; real-time, so run alone
        Case('audio/host', ['--host', '{inst10}'], serial=True, mask=[MS]),
        Case('audio/fevdeps', ['--fevdeps', '{inst10}'], serial=True),
        Case('audio/mixer', ['--mixer', '{inst10}'], serial=True),
        Case('audio/fmod', ['--fmod', '{inst10}', '{inputs}/names.tsv'] + [f + '.fev' for f in FEVS], serial=True,
             sort=r'EV\t'),   # EV: audio::Host::byId, an unordered_map
        Case('audio/vo', ['--vo', '{inst10}', '{out}', '{inputs}/dlg30'], serial=True,
             mask=[(r'at \d+ ms wall: position \d+ ms', 'at # ms wall: position # ms'), (r'after \d+ ms', 'after # ms'),
                   (r'^POS \d+ samples', 'POS # samples')], drop=[r'^POS ED019 at ']),
        Case('audio/voplay', ['--voplay', '{inst10}', 'Edna.fev', 'dialog_edna', 'Edna_str.fsb', 'ED019'], serial=True,
             mask=[(r' ev \w+$', ' ev #')], drop=[r'^INFO t ']),   # INFO: the playback clock, sampled each 250 ms
    ]
    return c


# ---- running and normalising ----

def sha(b):
    return hashlib.sha256(b).hexdigest()


def fill(s, roots, case):
    for k, v in roots.items():
        s = s.replace('{%s}' % k, v)
    missing = re.findall(r'\{(%s)\}' % '|'.join(ROOTS), s)
    if missing:
        raise SystemExit('%s needs --%s' % (case.name, missing[0]))
    return s


def materialise(case, roots, work):
    """-> argv with placeholders filled, globs expanded and templates written to <work>/in."""
    argv = []
    for a in case.args:
        if isinstance(a, str):
            argv.append(fill(a, roots, case))
        elif a[0] == 'glob':
            hits = sorted(glob.glob(fill(a[1], roots, case)))
            if not hits:
                raise SystemExit('%s: no file matches %s' % (case.name, a[1]))
            argv += hits
        else:
            src = Path(fill(a[1], roots, case))
            lines = fill(src.read_text(encoding='latin-1'), roots, case).splitlines(True)
            dst = work / 'in' / src.with_suffix('.txt').name
            dst.write_text(''.join(lines[:a[2]] if a[2] else lines), encoding='latin-1')
            argv.append(str(dst))
    return argv


def path_rx(p):
    return re.compile(r'[\\/]'.join(re.escape(x) for x in re.split(r'[\\/]', str(p).rstrip('\\/'))), re.I)


def normalise(text, case, subst, sort):
    out = []
    for line in text.replace('\r\n', '\n').split('\n'):
        for rx, ph in subst:
            line = rx.sub(ph, line)
        if any(re.search(d, line) for d in case.drop):
            continue
        for rx, rep in case.mask:
            line = re.sub(rx, rep, line)
        out.append(line)
    if sort is True:
        out.sort()
    elif sort:   # the matching lines are sorted among themselves; every other line keeps its place
        at = [i for i, x in enumerate(out) if re.match(sort, x)]
        for i, x in zip(at, sorted(out[i] for i in at)):
            out[i] = x
    return '\n'.join(out)


def run_case(case, octtest, roots, work):
    if work.exists():
        shutil.rmtree(work)
    out = work / 'files'
    out.mkdir(parents=True)
    (work / 'in').mkdir()
    r = dict(roots, out=str(out), **{'in': str(work / 'in')})
    t = time.time()
    if case.name in STATIC:
        try:
            code, so, se = 0, STATIC[case.name](), ''
        except SystemExit as e:
            code, so, se = 1, '', str(e)
    else:
        env = dict(os.environ, **{k: fill(v, r, case) for k, v in case.env.items()})
        try:
            p = subprocess.run([str(octtest)] + materialise(case, r, work), capture_output=True, env=env, cwd=work,
                               timeout=3600)
            code, so, se = p.returncode, p.stdout.decode('latin-1'), p.stderr.decode('latin-1')
        except subprocess.TimeoutExpired:
            code, so, se = 'timeout', '', ''
    secs = time.time() - t
    subst = sorted(((path_rx(v), '{%s}' % k) for k, v in r.items() if v), key=lambda x: -len(x[0].pattern))
    so, se = normalise(so, case, subst, case.sort), normalise(se, case, subst, case.sort_err)
    (work / 'stdout.txt').write_bytes(so.encode('latin-1'))
    (work / 'stderr.txt').write_bytes(se.encode('latin-1'))
    files = {f.relative_to(out).as_posix(): sha(f.read_bytes()) for f in sorted(out.rglob('*')) if f.is_file()}
    return {'exit': code, 'stdout': sha(so.encode('latin-1')), 'stderr': sha(se.encode('latin-1')), 'files': files,
            'seconds': round(secs, 1)}


def run_all(sel, octtest, roots, base, jobs):
    """Runs the selected cases, the serial ones alone after the rest. -> {name: result}"""
    res = {}

    def one(c):
        res[c.name] = run_case(c, octtest, roots, base / c.name)
        print('  ran %-34s %7.1f s' % (c.name, res[c.name]['seconds']), flush=True)
    with ThreadPoolExecutor(jobs) as ex:
        list(ex.map(one, [c for c in sel if not c.serial]))
    for c in sel:
        if c.serial:
            one(c)
    return res


# ---- comparing ----

def text_diff(a, b, n=12):
    d = list(difflib.unified_diff(a.splitlines(), b.splitlines(), 'baseline', 'now', lineterm='', n=0))[2:]
    return d[:n] + (['... %d more diff lines' % (len(d) - n)] if len(d) > n else [])


def file_diff(a, b):
    if b'\0' not in a[:4096] + b[:4096]:
        return text_diff(a.decode('latin-1'), b.decode('latin-1'), 6)
    at = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]), min(len(a), len(b)))
    return ['%d B vs %d B, first difference at offset %#x' % (len(a), len(b), at)]


def compare(old, new, odir, ndir):
    """-> diff lines, empty when the case matches its baseline."""
    d = []
    if old['exit'] != new['exit']:
        d.append('exit %s, baseline %s' % (new['exit'], old['exit']))
    for part in ('stdout', 'stderr'):
        if old[part] != new[part]:
            d.append(part + ':')
            d += text_diff((odir / (part + '.txt')).read_bytes().decode('latin-1'),
                           (ndir / (part + '.txt')).read_bytes().decode('latin-1'))
    of, nf = old['files'], new['files']
    for f in sorted(set(of) | set(nf)):
        if f not in nf:
            d.append('file missing: ' + f)
        elif f not in of:
            d.append('file new: ' + f)
        elif of[f] != nf[f]:
            d.append('file differs: ' + f)
            d += ['  ' + x for x in file_diff((odir / 'files' / f).read_bytes(), (ndir / 'files' / f).read_bytes())]
        if len(d) > 40:
            return d + ['...']
    return d


# ---- static facts from the plugin sources (no octtest) ----

TOK = re.compile(r'''(?P<ws>\s+)|(?P<com>//[^\n]*|/\*.*?\*/)
    |(?P<str>(?:u8|[uUL])?R"(?P<dl>[^(\s]*)\(.*?\)(?P=dl)"|(?:u8|[uUL])?"(?:\\.|[^"\\\n])*")
    |(?P<chr>'(?:\\.|[^'\\\n])*')|(?P<num>\.?\d(?:[eEpP][+-]|[\w.'])*)|(?P<id>[A-Za-z_]\w*)
    |(?P<op>::|->|[!=<>]=|&&|\|\||\S)''', re.S | re.X)
LOG_SINKS = {'logf', 'logfn', 'AUDLOG', 'g_log', 'log'}   # logf and the pointers the plugin sets to it
ROW_CALL, FEATURE_CALL, ENTRY = 'hook', 'installFeature', 'Pentane_Main'


def plugin_sources():
    """Every first-party .hpp/.cpp of the plugin: not octtest.cpp, not minhook/ or third_party/."""
    fs = [p for p in sorted(SRC.rglob('*')) if p.suffix in ('.hpp', '.cpp') and p.name != 'octtest.cpp'
          and not {'minhook', 'third_party'} & set(p.relative_to(SRC).parts)]
    if not fs:
        raise SystemExit('no plugin sources under %s' % SRC)
    return fs


def tokens(path):
    """-> [(kind, text)] without comments, whitespace and preprocessor lines."""
    text = re.sub(r'^[ \t]*#(?:[^\n]*\\\n)*[^\n]*', '', path.read_text(encoding='latin-1'), flags=re.M)
    return [(m.lastgroup, m.group()) for m in TOK.finditer(text) if m.lastgroup not in ('ws', 'com')]


def close(toks, i):
    """Index of the bracket matching the opening bracket toks[i]."""
    depth = 0
    for j in range(i, len(toks)):
        t = toks[j][1]
        if t in ('(', '[', '{'):
            depth += 1
        elif t in (')', ']', '}'):
            depth -= 1
            if depth == 0:
                return j
    raise SystemExit('unbalanced brackets near token %d' % i)


def split_args(toks, i):
    """toks[i] is an opening bracket -> its top-level comma-separated items as token lists."""
    items, cur, j, end = [], [], i + 1, close(toks, i)
    while j < end:
        if toks[j][1] in ('(', '[', '{'):
            k = close(toks, j)
            cur += toks[j:k + 1]
            j = k + 1
            continue
        if toks[j][1] == ',':
            items.append(cur)
            cur = []
        else:
            cur.append(toks[j])
        j += 1
    return [x for x in items + [cur] if x]


def literal(item):
    """A string-literal item ("a" "b" concatenated) -> its source text, else None."""
    if not item or any(k != 'str' for k, _ in item):
        return None
    return ''.join(t[t.index('"') + 1:t.rindex('"')] for _, t in item)


def functions(toks):
    """-> [(qualified name, body open, body close)] of every function defined outside another function."""
    out, stack, start, i = [], [], 0, 0   # stack: (name, close index) of the namespaces and classes we are in
    while i < len(toks):
        while stack and i > stack[-1][1]:
            stack.pop()
        t = toks[i][1]
        if t in (';', '}'):
            start = i + 1
        if t != '{':
            i += 1
            continue
        end, stmt = close(toks, i), [x for _, x in toks[start:i]]
        scope = '::'.join(n for n, _ in stack if n)
        kw = [x for x in stmt if x in ('namespace', 'struct', 'class', 'union', 'enum')]
        if 'namespace' in stmt or stmt == ['extern', '"C"']:
            stack.append((''.join(stmt[stmt.index('namespace') + 1:]) if kw else '', end))
        elif kw and '(' not in stmt and '=' not in stmt:
            name = stmt[stmt.index(kw[0]) + 1:stmt.index(kw[0]) + 2]
            stack.append((name[0] if name and re.match(r'[A-Za-z_]\w*$', name[0]) and name[0] != 'class' else '', end))
        else:
            j = i - 1
            while j >= start and toks[j][1] in ('const', 'noexcept', 'override', 'final'):
                j -= 1
            if toks[j][1] == ')':
                op = next(k for k in range(j, -1, -1) if toks[k][1] == '(' and close(toks, k) == j)
                name, k = [toks[op - 1][1]] if toks[op - 1][0] == 'id' else [], op - 1
                while name and k >= 2 and toks[k - 1][1] == '::' and toks[k - 2][0] == 'id':
                    name[:0] = [toks[k - 2][1], '::']
                    k -= 2
                if name:
                    out.append(('::'.join(x for x in (scope, ''.join(name)) if x), i, end))
            i = end   # a function body or an initialiser: nothing inside it is a top-level definition
        start = i + 1
        i += 1
    return out


def log_formats():
    fmts = {}
    for f in plugin_sources():
        toks = tokens(f)
        for i in range(1, len(toks) - 2):
            if toks[i][1] in LOG_SINKS and toks[i + 1][1] == '(' and toks[i - 1][1] not in ('.', '->'):
                args = split_args(toks, i + 1)
                s = literal(args[0]) if args else None
                if s is not None:
                    fmts[s] = fmts.get(s, 0) + 1
    if not fmts:
        raise SystemExit('static/logf: no logf format string found under %s' % SRC)
    return '\n'.join('%d\t%s' % (n, s) for s, n in sorted(fmts.items()))


def row_args(a):
    """Items "name", 0xVA, "prologue", detour, original[, ...] -> (name, VA, prologue, detour, original), else None.
    detour and original are given by their last identifier, so a move that only qualifies them changes nothing."""
    if len(a) < 5 or literal(a[0]) is None or len(a[1]) != 1 or a[1][0][0] != 'num' or literal(a[2]) is None:
        return None
    last = [[x for k, x in it if k == 'id'][-1:] for it in a[3:5]]
    return (literal(a[0]), '%08X' % int(a[1][0][1].rstrip('uUlL'), 0), literal(a[2]), ''.join(last[0]),
            ''.join(last[1]))


class Facts:
    """Hook events per function (rows installed, calls made, in source order) and the brace-initialised variables."""

    def __init__(self):
        self.funcs, self.vars = {}, {}   # name -> [events]; variable -> [(file tokens, item tokens, position)]
        for f in plugin_sources():
            toks = tokens(f)
            for k in range(len(toks) - 3):   # name = {..} / name{..} / name.push_back(..): candidate row variables
                if toks[k][0] != 'id':
                    continue
                b = k + 2 if toks[k + 1][1] == '=' else k + 1
                if toks[k + 1][1] in ('=', '{') and toks[b][1] == '{':
                    self.vars.setdefault(toks[k][1], []).append((toks, toks[b:close(toks, b) + 1], b))
                if toks[k + 1][1] == '.' and toks[k + 2][1] in ('push_back', 'emplace_back') and toks[k + 3][1] == '(':
                    for it in split_args(toks, k + 3)[:1]:
                        self.vars.setdefault(toks[k][1], []).append((toks, it, k + 3))
            for name, b, e in functions(toks):
                self.funcs.setdefault(name, []).extend(self.events(toks, b, e))

    def events(self, toks, b, e):
        ev = []
        for i in range(b + 1, e - 1):
            if toks[i + 1][1] != '(' or toks[i][0] != 'id' or toks[i - 1][1] in ('.', '->'):
                continue
            if toks[i][1] == ROW_CALL:
                r = row_args(split_args(toks, i + 1))
                if r:
                    ev.append(('rows', '-', [r]))
            elif toks[i][1] == FEATURE_CALL:
                a = split_args(toks, i + 1)
                if len(a) == 2 and literal(a[0]) is not None:
                    ev.append(('rows', literal(a[0]), self.rows_of(a[1], toks, b, i)))
            else:
                k, name = i, [toks[i][1]]
                while k >= 2 and toks[k - 1][1] == '::' and toks[k - 2][0] == 'id':
                    name[:0] = [toks[k - 2][1], '::']
                    k -= 2
                ev.append(('call', ''.join(name), None))
        return ev

    def rows_of(self, item, toks, b, at):
        """Rows of an installFeature argument: a brace list of rows and row variables, or a row-list variable
        (its initialiser and push_backs in the function before the call, else a table defined elsewhere)."""
        if item[0][1] == '{':
            r = row_args(split_args(item, 0))
            return [r] if r else [x for it in split_args(item, 0) for x in self.rows_of(it, toks, b, at)]
        if len(item) == 1 and item[0][0] == 'id':
            defs = self.vars.get(item[0][1], [])
            use = [(p, it) for t, it, p in defs if t is toks and b < p < at]
            if not use and len(defs) == 1:
                use = [(defs[0][2], defs[0][1])]
            rows = [r for _, it in sorted(use, key=lambda x: x[0]) for r in self.rows_of(it, toks, b, at)]
            if rows:
                return rows
        raise SystemExit('static/hooks: cannot read the hook rows of %s' % ' '.join(x for _, x in item))

    def resolve(self, call, caller):
        scope = caller.split('::')[:-1]
        for n in range(len(scope), -1, -1):
            q = '::'.join(scope[:n] + [call])
            if q in self.funcs:
                return q
        return None


def hook_rows():
    F = Facts()
    has = {n for n, ev in F.funcs.items() if any(k == 'rows' for k, _, _ in ev)}
    while True:   # and the functions that reach a row through their calls
        more = {n for n, ev in F.funcs.items() if n not in has
                and any(k == 'call' and F.resolve(x, n) in has for k, x, _ in ev)}
        if not more:
            break
        has |= more
    roots = [n for n in F.funcs if n.split('::')[-1] == ENTRY]
    if len(roots) != 1:
        raise SystemExit('static/hooks: expected one %s, found %d' % (ENTRY, len(roots)))
    lines, reached = [], set()

    def walk(fn, active):
        reached.add(fn)
        for k, x, rows in F.funcs[fn]:
            if k == 'rows':
                lines.extend('\t'.join((x,) + r) for r in rows)
            elif F.resolve(x, fn) in has and F.resolve(x, fn) not in active:
                walk(F.resolve(x, fn), active | {fn})
    walk(roots[0], set())
    for fn in sorted(has - reached):   # rows no call from the entry reaches: listed, never dropped
        lines.extend('\t'.join(('unreached ' + fn + ': ' + x,) + r) for k, x, rows in F.funcs[fn] if k == 'rows'
                     for r in rows)
    if not lines:
        raise SystemExit('static/hooks: no hook rows found under %s' % SRC)
    return '\n'.join(lines)


def open_of(toks, j):
    """Index of the opening bracket matching the closing bracket toks[j]."""
    depth = 0
    for k in range(j, -1, -1):
        depth += toks[k][1] in (')', ']', '}')
        depth -= toks[k][1] in ('(', '[', '{')
        if depth == 0:
            return k
    raise SystemExit('unbalanced brackets near token %d' % j)


def config_keys():
    """key, kind and default of every config key. The strcmp chain maps key -> object.member (the largest chain over
    one object is the config); the struct that object is declared with gives the defaults."""
    chains, files = {}, [tokens(f) for f in plugin_sources()]
    for toks in files:
        for i in range(len(toks) - 6):
            if toks[i][1] != 'strcmp' or toks[i + 1][1] != '(' or toks[i + 4][0] != 'str' or toks[i + 5][1] != ')':
                continue
            j = i + 6
            while toks[j][1] == ')':
                j += 1
            stmt = []
            while j > i + 6 and toks[j][1] != ';':   # if (!strcmp(key, "k")) <statement>;
                stmt.append(toks[j][1])
                j += 1
            at = 3 if stmt[:2] == ['strcpy_s', '('] else 1 if stmt[3:4] == ['='] else None
            if at is None or stmt[at:at + 1] != ['.']:
                continue
            kind = 'str' if at == 3 else 'bool' if '!=' in stmt else 'int' if 'atoi' in stmt else '?'
            chains.setdefault(stmt[at - 1], []).append((literal([toks[i + 4]]), kind, stmt[at + 1]))
    if not chains:
        raise SystemExit('static/config: no config key found under %s' % SRC)
    obj, keys = max(chains.items(), key=lambda x: len(x[1]))
    body, members = struct_of(files, obj), {}
    for k in range(len(body) - 3):   # type name [N] = value ;
        if body[k][0] == 'id' and body[k + 1][0] == 'id':
            j, size = k + 2, ''
            if body[j][1] == '[':
                size, j = body[j + 1][1], j + 3
            if body[j][1] == '=' and body[j + 2][1] == ';':
                members[body[k + 1][1]] = (size, body[j + 1][1])
    out = []
    for key, kind, field in keys:
        if field not in members:
            raise SystemExit('static/config: no default for %s (%s.%s)' % (key, obj, field))
        size, val = members[field]
        val = {'false': '0', 'true': '1'}.get(val, val)
        out.append('%s\t%s\t%s' % (key, kind + ('[%s]' % size if size else ''), val))
    return '\n'.join(sorted(out))


def struct_of(files, obj):
    """Tokens of the struct an object is declared with: struct { .. } obj;  or  Type obj; with struct Type { .. }."""
    for toks in files:
        for k in range(1, len(toks) - 1):
            if toks[k][1] != obj or toks[k + 1][1] not in (';', '{', '='):
                continue
            if toks[k - 1][1] == '}':
                return toks[open_of(toks, k - 1):k]
            for t in files if toks[k - 1][0] == 'id' else ():
                for j in range(len(t) - 2):
                    if t[j][1] in ('struct', 'class') and t[j + 1][1] == toks[k - 1][1] and t[j + 2][1] == '{':
                        return t[j + 2:close(t, j + 2) + 1]
    raise SystemExit('static/config: no struct found for %s' % obj)


STATIC = {'static/logf': log_formats, 'static/hooks': hook_rows, 'static/config': config_keys}


# ---- command line ----

def select(a):
    allc = [Case(n, [], quick=True) for n in STATIC] + cases()
    sel = [c for c in allc if (c.quick or not a.quick)
           and (not a.cases or any(fnmatch.fnmatch(c.name, g) for g in a.cases))]
    if not sel:
        raise SystemExit('no case matches')
    return sel


def octtest_of(a, need):
    exe = Path(a.octtest).resolve() if a.octtest else HERE / 'build' / 'Release' / 'octtest.exe'
    if need and not exe.is_file():
        raise SystemExit('octtest not found: %s (pass --octtest)' % exe)
    return exe


def given_roots(a, all_found):
    """The roots on the command line (with all_found: also from the environment and Steam, paths.py)."""
    r = {}
    for k in paths.ENV:
        p = paths.where(k)[0] if all_found else getattr(a, 'root_' + k)
        if p:
            r[k] = str(Path(p).resolve())
    if a.inst10:
        r['inst10'] = str(Path(a.inst10).resolve())
    return r


def main():
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument('--golden', default=os.environ.get('DI_GOLDEN'), help='golden root (default: $DI_GOLDEN)')
    common.add_argument('--octtest', help='octtest.exe to run (default: build/Release/octtest.exe)')
    common.add_argument('--inst10', help='the 1.0 install (assets\\ holds its zips; audio and archive cases)')
    common.add_argument('--quick', action='store_true', help='only the fast subset')
    common.add_argument('-j', type=int, default=os.cpu_count(), help='cases run at once (default: every core)')
    paths.add_args(common)
    ap = argparse.ArgumentParser(description='Record and check the golden baseline of octtest and the plugin sources',
                                 formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    sub = ap.add_subparsers(dest='cmd', required=True)
    rec = sub.add_parser('record', parents=[common], help='run the cases, store them as baseline <label>')
    rec.add_argument('label')
    chk = sub.add_parser('check', parents=[common], help='run the cases, compare with a baseline')
    chk.add_argument('--baseline', help='label (default: the newest baseline)')
    for p in (rec, chk, sub.add_parser('list', parents=[common], help='list the cases')):
        p.add_argument('cases', nargs='*', help='case name globs, given together (default: all)')
    a = ap.parse_args()
    sel = select(a)
    if a.cmd == 'list':
        for x in sel:
            print('%-34s %-6s %s' % (x.name, 'quick' if x.quick else '', ' '.join(map(str, x.args))))
        return
    if not a.golden:
        raise SystemExit('pass --golden <dir> or set DI_GOLDEN')
    G = Path(a.golden).resolve()
    need = any(x.name not in STATIC for x in sel)
    t0 = time.time()
    if a.cmd == 'record':
        exe, roots = octtest_of(a, need), dict(given_roots(paths.use(a), True), inputs=str(G / 'inputs'))
        base = G / a.label
        res = run_all(sel, exe, roots, base / 'cases', a.j)
        meta = {'label': a.label, 'created': time.strftime('%Y-%m-%d %H:%M:%S'), 'octtest': str(exe),
                'octtest_sha256': sha(exe.read_bytes()) if need else None, 'roots': roots, 'cases': res}
        (base / 'meta.json').write_text(json.dumps(meta, indent=1, sort_keys=True))
        bad = [n for n in STATIC if n in res and res[n]['exit']]
        for n in bad:
            print('FAIL %s: %s' % (n, (base / 'cases' / n / 'stderr.txt').read_text()))
        print('recorded %d cases as %s in %.0f s' % (len(res), a.label, time.time() - t0))
        sys.exit(1 if bad else 0)
    if a.baseline:
        mf = G / a.baseline / 'meta.json'
    else:
        mf = max(G.glob('*/meta.json'), key=lambda p: p.stat().st_mtime, default=None)
    if not mf or not mf.is_file():
        raise SystemExit('no baseline %s under %s' % (a.baseline or '', G))
    meta = json.loads(mf.read_text())
    run = G / '_check' / ('%s-%d' % (time.strftime('%Y%m%d-%H%M%S'), os.getpid()))   # lanes may check at once
    res = run_all(sel, octtest_of(a, need), dict(meta['roots'], **given_roots(a, False)), run, a.j)
    failed = 0
    for x in sel:
        old = meta['cases'].get(x.name)
        d = ['not in baseline ' + meta['label']] if old is None else \
            compare(old, res[x.name], mf.parent / 'cases' / x.name, run / x.name)
        print('%s %-34s %7.1f s' % ('FAIL' if d else 'ok  ', x.name, res[x.name]['seconds']))
        for line in d:
            print('     ' + line)
        failed += bool(d)
    print('%d of %d cases differ from %s (%.0f s)' % (failed, len(sel), meta['label'], time.time() - t0))
    if failed:
        print('outputs kept in ' + str(run))
    else:
        shutil.rmtree(run)
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
