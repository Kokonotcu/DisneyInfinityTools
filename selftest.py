"""Run every tool's self-test against the configured roots and report one line each.

A test whose roots are not set is skipped, not failed. Each runs as its own process (they fan out over every
core themselves) with the same --game / --ext10 / --ext30 / DI_* roots. Save files are only read.

python selftest.py [--ext30 DIR] [--ext10 DIR] [--game DIR] [--saves DIR] [--only oct di3arc ...]
"""
import argparse, os, subprocess, sys, time
from pathlib import Path
import paths

HERE = Path(__file__).parent
# name, command, roots it needs (all), roots it can also use
TESTS = [
    ('oct', ['oct.py', '--selftest'], (), ('ext30', 'ext10')),
    ('di3arc', ['di3arc.py', '--selftest'], ('game',), ()),
    ('matp', ['matp.py', '--selftest'], ('ext30',), ()),
    ('vtxf', ['vtxf.py', '--selftest'], ('ext30',), ('ext10',)),
    ('graph', ['graph.py', '--selftest'], ('ext30',), ()),
    ('btdblocks', ['btdblocks.py', '--selftest'], (), ('ext30', 'ext10')),
    ('btdconv', ['btdconv.py', '--selftest'], ('ext30',), ('ext10',)),
    ('realm30', ['realm30.py', '--selftest'], ('ext30', 'ext10'), ()),
    ('luabc', ['luabc.py', '--selftest'], ('ext30',), ()),
    ('dct', ['dct.py', '--selftest'], ('ext30',), ()),
    ('mer', ['mer.py', '--selftest'], ('ext30',), ()),
    ('chd', ['chd.py', '--selftest'], ('ext30',), ()),
    ('meshexport', ['meshexport.py', '--check'], ('ext30',), ()),
    ('streamlayout', ['streamlayout.py'], ('ext30',), ()),
    ('slotcheck', ['slotcheck.py'], ('ext30',), ()),
    ('savegame', ['savegame.py', '--selftest', '{saves}'], ('saves',), ()),
]


def default_saves():
    """The 3.0 PC save folder (%APPDATA%/Disney Interactive/Disney Infinity 3.0), if any."""
    p = Path(os.environ.get('APPDATA', '')) / 'Disney Interactive' / 'Disney Infinity 3.0'
    return p if p.is_dir() else None


def main():
    ap = argparse.ArgumentParser(description='Run every self-test')
    ap.add_argument('--saves', help='save folder for savegame.py (read only; default: the 3.0 PC save folder)')
    ap.add_argument('--only', nargs='*', default=[], help='run only these tests')
    paths.add_args(ap)
    a = paths.use(ap.parse_args())
    have = {r for r in paths.ENV if paths.where(r)[0]}
    saves = a.saves or default_saves()
    if saves:
        have.add('saves')
    rows, failed = [], 0
    for name, cmd, needs, can in TESTS:
        if a.only and name not in a.only:
            continue
        missing = [r for r in needs if r not in have]
        if missing or (not needs and not any(r in have for r in can)):
            rows.append((name, 'skip', 0.0, 'needs ' + ', '.join(missing or can)))
            continue
        t = time.time()
        p = subprocess.run([sys.executable, str(HERE / cmd[0])] + [str(saves) if c == '{saves}' else c for c in cmd[1:]],
                           capture_output=True, text=True, encoding='utf-8', errors='replace', cwd=HERE)
        lines = [l for l in (p.stdout + p.stderr).splitlines() if l.strip()]
        ok = p.returncode == 0
        failed += not ok
        rows.append((name, 'ok' if ok else 'FAIL', time.time() - t, lines[-1] if lines else ''))
        print('%-13s %-4s %6.1fs  %s' % rows[-1], flush=True)
    for r in rows:
        if r[1] == 'skip':
            print('%-13s %-4s %6s   %s' % (r[0], r[1], '', r[3]))
    print('%d passed, %d failed, %d skipped' % (sum(r[1] == 'ok' for r in rows), failed, sum(r[1] == 'skip' for r in rows)))
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
