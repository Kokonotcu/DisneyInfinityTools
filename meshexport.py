"""Export a world's (or exploder's / realm's) geometry to Wavefront OBJ, in world space, for inspection.

Buffers are resolved the way the records describe them, for both games:
  Vdata  [streams, vertices, (vertex buffer #, byte offset, stride) per stream] -> VertexBufferPool FileName
  Idata  [index buffer #, start, base vertex, count] -> IndexBufferPool Width + FileName
         3.0: start in BYTES, values node-global (vertex = value - base); 1.0: start in INDICES, values
         relative to the primitive's own streams (conv.build_buffers)
Positions (POSITION), UVs (TEXCOORD0, v flipped) and normals (NORMAL) decode through vtxf; nodes are placed by
their LocalToParentMatrix chain. One OBJ object per geometry node, one usemtl per primitive (material name).
The game is told by the vertex formats: every vformatCRC a 3.0 format -> 3.0 rules (converted worlds too).
--textures: an .mtl beside the OBJ, each material's colour (diffuse0) and normal (normalMap0) texture -- slots by
their role (shdfmt.slot_roles) in 3.0, the first texture in 1.0 -- as PNG in <out>_textures/. A .tbody (a DDS) is
looked up in the world's textures/ folder, the install's loose assets/textures/, the 3.0 extract's pool, then
straight inside the install's textures/<first byte>.zip.

python meshexport.py <world dir | .oct> <out.obj> [--all] [--node NAME ...] [--textures]
                     (--all: LOD-hidden nodes too)
python meshexport.py --check [--ext30 DIR]   every primitive of every shipped 3.0 tuple file stays inside its
                                             buffers under the 3.0 rule
"""
import argparse, math, struct
from collections import Counter
from pathlib import Path
from oct import Oct
from conv import geo_nodes, world_matrices
import paths, vtxf


def _pool_files(s, pool, rec):
    """{buffer #: (Width or None, FileName or None)} for VertexBufferPool / IndexBufferPool."""
    out = {}
    for i, r in enumerate(s.find(name=rec, pool=pool)):
        k = s.kids(r)
        out[i] = (k['Width'].ints()[0] if 'Width' in k else None,
                  k['FileName'].names()[0] if 'FileName' in k else None)
    return out


def game_of(s):
    known = {vtxf.name_hash(g) for g in vtxf.formats(3)}
    return 3 if all(r.ints()[0] & 0xffffffff in known for r in s.find(name='vformatCRC')) else 1


class Buffers:
    def __init__(self, s, folder):
        self.folder, self._raw = Path(folder), {}
        self.vb = _pool_files(s, 'VertexBufferPool', 'VertexBuffer')
        self.ib = _pool_files(s, 'IndexBufferPool', 'IndexBuffer')

    def raw(self, name):
        if name not in self._raw:
            self._raw[name] = (self.folder / name).read_bytes()
        return self._raw[name]

    def indices(self, idata, game):
        """-> (vertex numbers relative to the primitive's streams, the index buffer's length in indices)"""
        buf, start, base, count = idata
        w, fn = self.ib[buf]
        raw = self.raw(fn)
        off = start if game == 3 else start * w
        vals = struct.unpack_from('<%d%s' % (count, {1: 'B', 2: 'H', 4: 'I'}[w]), raw, off)
        return [v - base for v in vals] if game == 3 else list(vals), len(raw) // w

    def streams(self, vdata):
        """-> [(bytes of the vertex buffer, byte offset, stride)] per stream"""
        return [(self.raw(self.vb[vdata[2 + 3 * i]][1]), vdata[3 + 3 * i], vdata[4 + 3 * i]) for i in range(vdata[0])]


class Textures:
    """Colour / normal texture of each material of a world's bundle, written as PNG for the .mtl."""
    UNSET = 0xffffffff

    def __init__(self, oct_path, game, folder):
        import matp
        self.game, self.world, self.folder = game, Path(oct_path).parent, Path(folder)
        mtb = Path(oct_path).with_suffix('.mtb')
        self.bnd = matp.Bundle(mtb.read_bytes(), game) if mtb.exists() else None
        self._zips, self._png, self.missing = {}, {}, Counter()

    def maps(self, ref):
        """{'map_Kd': .tbody hash, 'map_Bump': hash} of material ref (only the ones it has)."""
        if not self.bnd or not 0 <= ref < len(self.bnd.materials):
            return {}
        m = self.bnd.materials[ref]
        idx = self.bnd.tex_slots(m)
        roles = [None] * len(idx)
        if self.game == 3 and paths.where('ext30')[0]:
            import shdfmt
            roles = shdfmt.slot_roles(m['shader'])
        out = {}
        for key, want in (('map_Kd', 'diffuse0'), ('map_Bump', 'normalMap0')):
            i = next((i for i, r in enumerate(roles) if r == want and idx[i] != self.UNSET), None)
            if i is None and key == 'map_Kd':          # no roles (1.0, no 3.0 extract): the first texture
                i = next((i for i, (r, t) in enumerate(zip(roles, idx))
                          if t != self.UNSET and (r is None or 'diffuse' in r)), None)
            if i is not None:
                out[key] = self.bnd.textures[idx[i]].hex()
        return out

    def _tbody(self, h):
        name = '%s.tbody' % h
        for f in [self.world / 'textures' / name] + \
                 ([paths.game('assets', 'textures', name)] if paths.where('game')[0] else []) + \
                 ([paths.ext30('textures', h[:2], 'textures', name)] if paths.where('ext30')[0] else []):
            if f.exists():
                return f.read_bytes()
        if paths.where('game')[0]:
            z = paths.game('assets', 'textures', h[:2] + '.zip')
            if z.exists() and z not in self._zips:
                import di3arc
                self._zips[z] = di3arc.unpack_index(z)
            return self._zips.get(z, {}).get('textures/' + name)
        return None

    def png(self, h):
        """Relative path of h's PNG (written on first use), or None if it cannot be found / decoded."""
        if h not in self._png:
            self._png[h] = None
            raw = self._tbody(h)
            if raw is None:
                self.missing['not found'] += 1
            else:
                import io
                from PIL import Image
                try:
                    self.folder.mkdir(parents=True, exist_ok=True)
                    Image.open(io.BytesIO(raw)).save(self.folder / ('%s.png' % h))
                    self._png[h] = '%s/%s.png' % (self.folder.name, h)
                except Exception as e:
                    self.missing['undecodable (%s)' % type(e).__name__] += 1
        return self._png[h]

    def mtl(self, used):
        """.mtl text for {material name: MaterialReference}."""
        out = []
        for name, ref in used.items():
            out += ['newmtl %s' % name, 'Kd 1 1 1']
            for key, h in self.maps(ref).items():
                p = self.png(h)
                if p:
                    out.append('%s %s' % (key, p))
            out.append('')
        return '\n'.join(out)


def _elem(fmt, sem):
    """(stream, offset, type) of the first element whose semantic starts with sem."""
    _, where, _ = vtxf.layout(fmt)
    for (s, ty), (si, off) in where.items():
        if s.rstrip('0') == sem:
            return si, off, ty
    return None


def export(oct_path, out_path, all_nodes=False, only=(), textures=False):
    oct_path = Path(oct_path)
    if oct_path.is_dir():
        oct_path = oct_path / (oct_path.name + '.oct')
    s = Oct.load(oct_path)
    game = game_of(s)
    bufs = Buffers(s, oct_path.parent)
    W = world_matrices(s)
    nodes = s.find(name='Node', pool='SceneTreeNodePool')
    pos_of = {id(n): i for i, n in enumerate(nodes)}
    out, nv, nt, nvt, nvn, skipped, used = [], 0, 0, 0, 0, Counter(), {}
    out.append('# %s (%d.0 rules) -> meshexport.py' % (oct_path.name, game))
    out_path = Path(out_path)
    if textures:
        out.append('mtllib %s' % out_path.with_suffix('.mtl').name)
    for n, k, prims in geo_nodes(s):
        name = k['NodeName'].names()[0] if 'NodeName' in k else 'node%d' % pos_of[id(n)]
        if only and name not in only:
            continue
        if not all_nodes and 'Visible' in k and not k['Visible'].ints()[0]:
            skipped['hidden'] += 1
            continue
        m = W[pos_of[id(n)]]
        out.append('o %s' % name)
        for p in prims:
            pk = s.kids(p)
            fmt = vtxf.fmt_of(pk['vformatCRC'].ints()[0] & 0xffffffff, game)
            idx, _ = bufs.indices(pk['Idata'].ints(), game)
            streams = bufs.streams(pk['Vdata'].ints())
            nverts = pk['Vdata'].ints()[1]
            el = {sem: _elem(fmt, sem) for sem in ('POSITION', 'TEXCOORD', 'NORMAL')}
            if not el['POSITION']:
                skipped['no POSITION'] += 1
                continue

            def attr(sem, v):
                si, off, ty = el[sem]
                buf, base, stride = streams[si]
                return vtxf.decode(ty, buf[base + v * stride + off:base + v * stride + off + vtxf.SIZE[ty]])
            for v in range(nverts):
                x, y, z = (attr('POSITION', v) + [0.0, 0.0])[:3]
                out.append('v %.5f %.5f %.5f' % (x * m[0] + y * m[4] + z * m[8] + m[12],
                                                 x * m[1] + y * m[5] + z * m[9] + m[13],
                                                 x * m[2] + y * m[6] + z * m[10] + m[14]))
                if el['TEXCOORD']:
                    u, t = (attr('TEXCOORD', v) + [0.0])[:2]
                    out.append('vt %.5f %.5f' % (u, 1.0 - t))
                if el['NORMAL']:
                    a, b, c = (attr('NORMAL', v) + [0.0, 0.0])[:3]
                    w = [a * m[0] + b * m[4] + c * m[8], a * m[1] + b * m[5] + c * m[9], a * m[2] + b * m[6] + c * m[10]]
                    l = math.sqrt(sum(q * q for q in w)) or 1.0
                    out.append('vn %.4f %.4f %.4f' % tuple(q / l for q in w))
            ref = pk['MaterialReference'].ints()[0] if 'MaterialReference' in pk else -1
            mname = pk['MaterialName'].names()[0] if 'MaterialName' in pk else 'material%d' % ref
            used.setdefault(mname, ref)
            out.append('usemtl %s' % mname)
            for i in range(0, len(idx) - 2, 3):
                f = [nv + idx[i + j] + 1 for j in range(3)]
                ft = [nvt + idx[i + j] + 1 for j in range(3)] if el['TEXCOORD'] else None
                fn = [nvn + idx[i + j] + 1 for j in range(3)] if el['NORMAL'] else None
                out.append('f ' + ' '.join('%d/%s/%s' % (f[j], ft[j] if ft else '', fn[j] if fn else '') for j in range(3)))
            nv += nverts; nvt += nverts if el['TEXCOORD'] else 0; nvn += nverts if el['NORMAL'] else 0
            nt += len(idx) // 3
    out_path.write_text('\n'.join(out) + '\n')
    print('%s -> %s: %d vertices, %d triangles%s' % (oct_path.name, out_path, nv, nt,
                                                    ', skipped %s' % dict(skipped) if skipped else ''))
    if textures:
        tx = Textures(oct_path, game, out_path.with_name(out_path.stem + '_textures'))
        out_path.with_suffix('.mtl').write_text(tx.mtl(used))
        print('  %d materials, %d textures written%s' % (len(used), sum(1 for v in tx._png.values() if v),
                                                       ', missing %s' % dict(tx.missing) if tx.missing else ''))


def _check_one(p):
    """(primitives, [problems]) under the 3.0 rule."""
    try:
        s = Oct.load(p)
    except Exception:
        return 0, []
    try:
        bufs = Buffers(s, p.parent)
        n, bad = 0, []
        for _, _, prims in geo_nodes(s):
            for pr in prims:
                pk = s.kids(pr)
                if 'Idata' not in pk:
                    continue
                n += 1
                vd = pk['Vdata'].ints()
                idx, _ = bufs.indices(pk['Idata'].ints(), 3)
                if idx and not (0 <= min(idx) and max(idx) < vd[1]):
                    bad.append('%s: index %d..%d outside %d vertices' % (p.name, min(idx), max(idx), vd[1]))
                for buf, off, stride in bufs.streams(vd):
                    if off + vd[1] * stride > len(buf):
                        bad.append('%s: stream runs past its vbuf' % p.name)
        return n, bad
    except FileNotFoundError:
        return 0, []                         # buffers in another archive (sub-zones): not checkable here


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('src', nargs='?', help='world folder or .oct')
    ap.add_argument('out', nargs='?', help='.obj to write')
    ap.add_argument('--all', action='store_true', help='include nodes hidden by LOD (Visible = 0)')
    ap.add_argument('--node', nargs='*', default=[], help='only these node names')
    ap.add_argument('--textures', action='store_true', help='write an .mtl and PNG textures beside the OBJ')
    ap.add_argument('--check', action='store_true')
    paths.add_args(ap)
    a = paths.use(ap.parse_args())
    if a.check:
        from par import pmap
        res = pmap(_check_one, sorted(paths.ext30().rglob('*.oct')), chunksize=16)
        prims, bad = sum(r[0] for r in res), [b for r in res for b in r[1]]
        print('3.0 rule: %d primitives checked, %d outside their buffers' % (prims, len(bad)))
        for b in bad[:10]:
            print('  ', b)
        assert prims and not bad
    elif a.src and a.out:
        export(a.src, a.out, a.all, set(a.node), a.textures)
    else:
        ap.print_help()
