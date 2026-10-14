"""Install Pentane (third_party/pentane) into the 3.0 game folder so that it loads di1compat.

The game loads Pentane because its executable imports it. The import of bink2w32.dll is renamed to Pentane.dll in
place, and Pentane forwards the Bink calls to bink2w32.dll. That rename is the whole difference between the Breeze
mod's stock and patched executables (10 bytes).

This script:
  - makes that rename, keeping a backup of the executable as DisneyInfinity3.exe.prepentane;
  - copies Pentane.dll and Pentane/config.toml (an existing config is kept);
  - creates Pentane/Plugins;
  - makes sure config.toml lists di1compat.dll under enabled_plugins.

Run it again after Steam restores the executable (an update or a file verification). --revert restores the backup.

python pentane_install.py [--game <3.0 install>] [--dry] [--revert]
python pentane_install.py --selftest <stock exe> <patched exe>   (the patch must give the patched file byte for byte)
"""
import argparse, re, shutil, struct, sys
from pathlib import Path

import paths

SRC = Path(__file__).parent / 'third_party' / 'pentane'
OLD, NEW = b'bink2w32.dll', b'Pentane.dll'


def import_names(b):
    """-> [(file offset, name)] of a 32-bit PE's import directory, in order."""
    pe = struct.unpack_from('<I', b, 0x3c)[0]
    nsec, optsz = struct.unpack_from('<H', b, pe + 6)[0], struct.unpack_from('<H', b, pe + 20)[0]
    opt = pe + 24
    if struct.unpack_from('<H', b, opt)[0] != 0x10b:
        raise ValueError('not a 32-bit executable')
    secs = [struct.unpack_from('<IIII', b, opt + optsz + 40 * i + 8) for i in range(nsec)]

    def off(rva):
        for vs, va, rs, ro in secs:
            if va <= rva < va + max(vs, rs):
                return rva - va + ro
        raise ValueError('rva %x outside the sections' % rva)

    o, out = off(struct.unpack_from('<I', b, opt + 104)[0]), []   # data directory 1: imports
    while True:
        name_rva = struct.unpack_from('<I', b, o + 12)[0]
        if not name_rva:
            return out
        no = off(name_rva)
        out.append((no, b[no:b.index(b'\0', no)]))
        o += 20


def patch(b):
    """-> (patched bytes or None if already patched). Raises if the executable has no bink2w32.dll import."""
    names = import_names(b)
    if any(n.lower() == NEW.lower() for _, n in names):
        return None
    hit = [o for o, n in names if n.lower() == OLD]
    if not hit:
        raise ValueError('no bink2w32.dll import: is this the PC Gold Edition executable?')
    out = bytearray(b)
    out[hit[0]:hit[0] + len(OLD)] = NEW.ljust(len(OLD), b'\0')
    return bytes(out)


def enable_plugin(text, plugin='di1compat.dll'):
    """config.toml text with plugin in enabled_plugins (added last = lowest priority); None if already there."""
    m = re.search(r'^(enabled_plugins\s*=\s*\[)(.*?)(\])', text, re.M | re.S)
    if m is None:
        return text.rstrip() + '\n\n[plugins]\nenabled_plugins = ["%s"]\n' % plugin
    if re.search(r'["\']%s["\']' % re.escape(plugin), m.group(2), re.I):
        return None
    items = m.group(2).strip().rstrip(',')
    return text[:m.start(2)] + (items + ', ' if items else '') + '"%s"' % plugin + text[m.end(2):]


def main():
    ap = paths.add_args(argparse.ArgumentParser(description=__doc__.split('\n\n')[0]), 'game')
    ap.add_argument('--dry', action='store_true', help='print what would change, write nothing')
    ap.add_argument('--revert', action='store_true', help='restore the executable from its backup')
    ap.add_argument('--selftest', nargs=2, metavar=('STOCK', 'PATCHED'), help='check the patch against a known pair')
    a = paths.use(ap.parse_args())

    if a.selftest:
        got = patch(Path(a.selftest[0]).read_bytes())
        ok = got == Path(a.selftest[1]).read_bytes()
        print('selftest:', 'patched stock == patched file' if ok else 'MISMATCH')
        return 0 if ok else 1

    exe = paths.game('DisneyInfinity3.exe')
    bak = exe.with_name(exe.name + '.prepentane')
    if not exe.is_file():
        sys.exit('no DisneyInfinity3.exe in %s' % exe.parent)
    if a.revert:
        if not bak.is_file():
            sys.exit('no backup %s' % bak.name)
        print('restore', exe.name, 'from', bak.name)
        if not a.dry:
            shutil.copy2(bak, exe)
        return 0

    patched = patch(exe.read_bytes())
    if patched is None:
        print(exe.name, 'already imports Pentane.dll')
    else:
        print('patch', exe.name, '(bink2w32.dll -> Pentane.dll), backup', bak.name)
        if not a.dry:
            if not bak.exists():
                shutil.copy2(exe, bak)
            exe.write_bytes(patched)

    dll, cfg = exe.parent / 'Pentane.dll', exe.parent / 'Pentane' / 'config.toml'
    if not dll.exists() or dll.read_bytes() != (SRC / 'Pentane.dll').read_bytes():
        print('copy Pentane.dll')
        if not a.dry:
            shutil.copy2(SRC / 'Pentane.dll', dll)
    if not cfg.exists():
        print('copy Pentane/config.toml')
        if not a.dry:
            cfg.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(SRC / 'Pentane' / 'config.toml', cfg)
    else:
        text = enable_plugin(cfg.read_text(encoding='utf-8'))
        if text is not None:
            print('add di1compat.dll to enabled_plugins in Pentane/config.toml')
            if not a.dry:
                cfg.write_text(text, encoding='utf-8')
    plugins = exe.parent / 'Pentane' / 'Plugins'
    if not plugins.is_dir():
        print('create Pentane/Plugins')
        if not a.dry:
            plugins.mkdir(parents=True)
    print('done' if not a.dry else 'dry run: nothing written')
    return 0


if __name__ == '__main__':
    sys.exit(main())
