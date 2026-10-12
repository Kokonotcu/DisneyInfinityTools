"""Extract an install's archives into the tree the other tools read (--ext30 / --ext10).

Every archive X/name.zip under assets/ becomes the folder X/name/ holding its members (worlds/out_intro2.zip ->
worlds/out_intro2/out_intro2.oct, shaders/shaders.zip -> shaders/shaders/vformat.vtxf). 3.0 archives (PK\\xff\\xff
index, encrypted or not) are read through their index, as the game reads them (di3arc.unpack_index); 1.0 Gold
Edition archives are plain zips. Files that are not archives (sound banks, videos) are copied only with --loose.
Existing files are overwritten, so a rerun (or one --only subtree) is always safe.

python extract.py <install or assets dir> <out dir> [--only worlds shaders materials ...] [--loose]
  then  --ext30 <out dir>  (or DI_EXT30)  for every other tool; a 1.0 copy the same way for --ext10
"""
import argparse, os, shutil, sys, zipfile
from pathlib import Path
from par import pmap


def _long(p):
    """Windows refuses paths past 260 characters unless they carry the \\\\?\\ prefix (material names alone
    run past 120)."""
    s = str(p)
    return Path('\\\\?\\' + s) if os.name == 'nt' and len(s) > 240 and not s.startswith('\\\\?\\') else p


def members(z):
    """{member name: bytes} of a 3.0 index archive or a plain zip."""
    with open(z, 'rb') as f:
        head = f.read(4)
    if head == b'PK\x03\x04':
        with zipfile.ZipFile(z) as zf:
            return {i.filename: zf.read(i) for i in zf.infolist() if not i.is_dir()}
    import di3arc
    return di3arc.unpack_index(z)


def _one(job):
    z, dst = job
    try:
        files = members(z)
    except Exception as e:
        return z, 0, 0, repr(e)[:160]
    root, n = dst.resolve(), 0
    for name, data in files.items():
        out = (root / name).resolve()
        if root not in out.parents:
            return z, n, 0, 'member escapes its folder: %s' % name
        try:
            _long(out.parent).mkdir(parents=True, exist_ok=True)
            _long(out).write_bytes(data)
        except OSError as e:
            return z, n, 0, repr(e)[:160]
        n += 1
    return z, n, sum(map(len, files.values())), None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('src', help='install folder (holding assets/) or the assets folder itself')
    ap.add_argument('out', help='extract root; assets/ is created inside it')
    ap.add_argument('--only', nargs='*', default=[], help='subtrees of assets/ to extract, e.g. worlds shaders')
    ap.add_argument('--loose', action='store_true', help='also copy files that are not archives')
    a = ap.parse_args()
    src = Path(a.src)
    src = src / 'assets' if (src / 'assets').is_dir() else src
    out = Path(a.out) / 'assets'
    tops = [src / o for o in a.only] or [src]
    for t in tops:
        if not t.exists():
            sys.exit('%s does not exist' % t)
    files = sorted({p for t in tops for p in ([t] if t.is_file() else t.rglob('*')) if p.is_file()})
    arcs = [p for p in files if p.suffix.lower() == '.zip']
    jobs = [(z, out / z.relative_to(src).with_suffix('')) for z in arcs]
    total, bad = 0, []
    for z, n, size, err in pmap(_one, jobs, chunksize=4):
        if err:
            bad.append((z.relative_to(src), err))
        total += size
    if a.loose:
        for p in files:
            if p.suffix.lower() != '.zip':
                d = (out / p.relative_to(src)).resolve()
                _long(d.parent).mkdir(parents=True, exist_ok=True)
                shutil.copy2(p, _long(d))
    print('%d archives -> %s (%.1f GB)%s' % (len(arcs) - len(bad), out, total / 2 ** 30,
                                            '' if not a.loose else ', %d loose files copied' % (len(files) - len(arcs))))
    for z, e in bad:
        print('  FAIL', z, e)
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
