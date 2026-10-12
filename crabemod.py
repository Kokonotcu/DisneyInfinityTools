"""Turn converter output into a CrabeLoader mod, instead of files written into the install.

  python native30.py ... --assets "<game>/mods/metroville"     (realm30.py / expl30.py: the same --assets)
  python crabemod.py "<game>/mods/metroville" --name Metroville [--version 0.1.0] [--zip metroville.zip]

CrabeLoader's VFS (src/infrastructure/vfs_override_manager.cpp) indexes every file under <game>/mods/<folder>/ --
folders starting with '.' or '_' are skipped -- keyed on its path with a leading assets/ dropped, and serves it when
the game opens that path through the OS (CreateFile / GetFileAttributes / FindFirstFile); the alphabetically later
mod wins. It needs no entry script (ModManager.cpp only runs one that exists). It cannot change a member INSIDE an
archive: where the game reads a path out of a shipped archive, a loose copy is never asked for -- --check lists the
mod's files whose folder a shipped X/name.zip covers, which are the ones to verify in-game first (or to deliver
inside the archive with di3arc.add_files / zones.py).

This writes mod.json (manifest v1: id, name, version, minLoaderVersion; no entry) and optionally a .zip of the
folder for sharing. The id must be reverse-DNS, lowercase (^[a-z0-9]+(\\.[a-z0-9-]+)+$); default local.<folder>.
"""
import argparse, json, re, sys, zipfile
from pathlib import Path
import paths

ID = re.compile(r'^[a-z0-9]+(\.[a-z0-9-]+)+$')


def default_id(folder):
    return 'local.' + (re.sub(r'[^a-z0-9-]+', '-', Path(folder).name.lower()).strip('-') or 'mod')


def shadowed(mod, assets):
    """Mod files whose path lies inside a folder a shipped archive X/name.zip provides."""
    out = []
    for f in sorted(p for p in Path(mod).rglob('*') if p.is_file() and p.name != 'mod.json'):
        rel = f.relative_to(mod).parts
        rel = rel[1:] if rel[0].lower() == 'assets' else rel
        for i in range(1, len(rel)):
            arc = Path(assets).joinpath(*rel[:i - 1], rel[i - 1] + '.zip')
            if arc.exists():
                out.append(('/'.join(rel), arc.relative_to(assets).as_posix()))
                break
    return out


def main():
    ap = argparse.ArgumentParser(description='Package converter output as a CrabeLoader mod')
    ap.add_argument('mod', help='the mod folder (<game>/mods/<folder>), as given to --assets')
    ap.add_argument('--id', help='reverse-DNS id (default: local.<folder>)')
    ap.add_argument('--name', help='display name (default: the folder name)')
    ap.add_argument('--version', default='0.1.0')
    ap.add_argument('--min-loader', default='0.2.0', help='minLoaderVersion')
    ap.add_argument('--description', default='Disney Infinity 1.0 content ported with DI3Tools')
    ap.add_argument('--zip', help='also write the folder as this .zip (to unpack into <game>/mods/)')
    ap.add_argument('--check', action='store_true', help="list files a shipped archive's folder covers")
    paths.add_args(ap, 'game')
    a = paths.use(ap.parse_args())
    mod = Path(a.mod)
    if not mod.is_dir():
        sys.exit('%s is not a folder' % mod)
    if mod.name.startswith(('.', '_')):
        sys.exit("CrabeLoader skips mod folders starting with '.' or '_'")
    mid = a.id or default_id(mod)
    if not ID.match(mid):
        sys.exit('id %r refused by CrabeLoader: reverse-DNS, lowercase, no underscores' % mid)
    manifest = {'manifestVersion': 1, 'id': mid, 'name': a.name or mod.name, 'version': a.version,
                'minLoaderVersion': a.min_loader, 'description': a.description}
    (mod / 'mod.json').write_text(json.dumps(manifest, indent=2) + '\n')
    files = [p for p in mod.rglob('*') if p.is_file()]
    print('%s: mod.json written (%s %s), %d files' % (mod, mid, a.version, len(files)))
    if a.check:
        sh = shadowed(mod, paths.game('assets'))
        for rel, arc in sh:
            print('  covered by %s: %s' % (arc, rel))
        print('%d of %d files lie under a shipped archive\'s folder' % (len(sh), len(files) - 1))
    if a.zip:
        with zipfile.ZipFile(a.zip, 'w', zipfile.ZIP_DEFLATED) as z:
            for p in sorted(files):
                z.write(p, (Path(mod.name) / p.relative_to(mod)).as_posix())
        print('zipped ->', a.zip)


if __name__ == '__main__':
    main()
