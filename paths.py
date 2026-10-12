"""Where things are: the 3.0 game install and the extracted 1.0 / 3.0 reference trees.

No tool assumes a machine. Each root resolves, first hit wins:
  1. the command line   --game / --ext10 / --ext30   (add_args + use, in every tool)
  2. the environment    DI3_GAME / DI_EXT10 / DI_EXT30
  3. game only          Steam's library list -> '<library>/steamapps/common/Disney Infinity 3.0 Gold Edition'
An extract root is the folder holding assets/ (extract.py writes one from an install or a 1.0 copy).
use() publishes the command line into the environment, so pmap's worker processes resolve the same roots.

python paths.py                    show every root and where it came from
python paths.py game|ext10|ext30   print one (the PowerShell tools call this)
"""
import os, re, sys
from pathlib import Path

ENV = {'game': 'DI3_GAME', 'ext10': 'DI_EXT10', 'ext30': 'DI_EXT30'}
GAME_DIR = 'Disney Infinity 3.0 Gold Edition'
HELP = {'game': '3.0 install folder (default: $DI3_GAME, else found through Steam)',
        'ext10': 'extracted 1.0 tree, the folder holding assets/ (default: $DI_EXT10)',
        'ext30': 'extracted 3.0 tree, the folder holding assets/ (default: $DI_EXT30)'}
HINT = {'game': 'pass --game <install dir> or set DI3_GAME',
        'ext10': 'pass --ext10 <dir holding assets/> or set DI_EXT10 (extract.py builds one)',
        'ext30': 'pass --ext30 <dir holding assets/> or set DI_EXT30 (extract.py builds one)'}


def add_args(ap, *roots):
    """Add --game / --ext10 / --ext30 (all, or the ones named) to an argparse parser."""
    for r in roots or ENV:
        ap.add_argument('--' + r, dest='root_' + r, metavar='DIR', help=HELP[r])
    return ap


def use(args):
    """Apply parsed --game/--ext10/--ext30 values (process-wide, inherited by worker processes)."""
    for r, var in ENV.items():
        v = getattr(args, 'root_' + r, None)
        if v:
            os.environ[var] = str(Path(v).resolve())
    return args


def _steam_libraries():
    roots = []
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r'Software\Valve\Steam') as k:
            roots.append(Path(winreg.QueryValueEx(k, 'SteamPath')[0]))
    except (ImportError, OSError):          # not Windows (Proton, CI), or no Steam key
        pass
    roots += [Path(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)')) / 'Steam',
              Path.home() / '.steam' / 'steam', Path.home() / '.local' / 'share' / 'Steam']
    libs = []
    for s in roots:
        vdf = s / 'steamapps' / 'libraryfolders.vdf'
        if vdf.exists():
            libs += [Path(p.replace('\\\\', '\\')) for p in re.findall(r'"path"\s+"([^"]+)"', vdf.read_text(errors='ignore'))]
        libs.append(s)
    return list(dict.fromkeys(libs))


def where(root):
    """-> (Path or None, source description)"""
    v = os.environ.get(ENV[root])
    if v:
        return Path(v), ENV[root]
    if root == 'game':
        for lib in _steam_libraries():
            p = lib / 'steamapps' / 'common' / GAME_DIR
            if (p / 'DisneyInfinity3.exe').exists():
                return p, 'Steam library'
    return None, 'not set'


def _root(root):
    p, _ = where(root)
    if p is None:
        raise SystemExit('%s not found: %s' % (root, HINT[root]))
    if not p.is_dir():
        raise SystemExit('%s: %s is not a folder (%s)' % (root, p, HINT[root]))
    return p


def cached(name, build, version=1):
    """A corpus measurement, kept as <this folder>/<name> (JSON) and rebuilt when its version changes. Callers
    store paths relative to an extract, so a cache survives the extract moving; delete the file after
    extracting another game build."""
    import json
    f = Path(__file__).with_name(name)
    if f.exists():
        c = json.loads(f.read_text())
        if isinstance(c, dict) and c.get('v') == version:
            return c['data']
    data = build()
    f.write_text(json.dumps({'v': version, 'data': data}))
    return data


def game(*parts):
    """The 3.0 install (DisneyInfinity3.exe, assets/), joined with parts."""
    return _root('game').joinpath(*parts)


def ext30(*parts):
    """The extracted 3.0 tree's assets/, joined with parts."""
    return _root('ext30').joinpath('assets', *parts)


def ext10(*parts):
    """The extracted 1.0 tree's assets/, joined with parts."""
    return _root('ext10').joinpath('assets', *parts)


if __name__ == '__main__':
    if sys.argv[1:2] and sys.argv[1] in ENV:
        print(_root(sys.argv[1]))
    else:
        for r in ENV:
            p, src = where(r)
            print('%-6s %-60s (%s)' % (r, p or '-', src))
