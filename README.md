# DI3Tools

Tools for porting Disney Infinity 1.0 worlds into 3.0 (PC Gold Edition). Only verified tools are kept here. The superseded experiments stay in `..\_tools`.
Format notes: https://claude.ai/artifact/FNnTo1rpHJ7UAWH7UoCooP

| Tool | What | Verified by |
|---|---|---|
| `oct.py` | `.oct` scene container read/modify/write. Writes the header `0x14` record count, which the engine allocates from | `--selftest`: byte-exact round-trip of every world `.oct` |
| `world30.py` | Converts any 1.0 world to 3.0 (`--source --world --offset --bundle`). Remaps `vformatCRC` to its declaration-identical 3.0 twin | sumo + `adv_cbt_protectmonolithsmall` load in-game; reproduces sumo byte-for-byte |
| `graph.py` | Meridian graph sizing per geometry node | `--selftest` re-derives dor_bay n=2 and lb_zonetwo n=3 exactly |
| `split-mesh.py` | Vertex/index buffer splitting: primitive-major (multi-node) and stream-major (multi-primitive node) | `--selftest` |
| `mtb-retex.py` | Retextures a shipped `.mtb` by swapping TEXB name hashes only. Use this instead of synthesising bundles | sumo's 3 textures rendered in-game |
| `matp.py` | `.mtb` codec from the engine's own parser (FUN_00472590). Per-material block sizes come from the shader (`.shd` u16 @0x6a + @0xa2), and block B opens with `shd[0xa1]` texture slots (u32 TEXB index). `merge()` combines materials from shipped bundles verbatim and only rebases texture indices | `--selftest`: layout predicted for 48,057/48,057 shipped bundles; 26,416/26,416 TEXB+MATP rebuilt byte-exact |
| `mtb-build.py` | `.mtb` grouped-header layout reader/builder (n<=2) | `--selftest` rebuilds stock out_intro2.mtb byte-exact |
| `btd-convert.ps1` | 1.0 -> 3.0 collision `.btd` (needs `btd-a-const.bin`), `-Offset` must match the mesh offset | 323/330 shipped pairs rebuilt byte-exact; collision works in-game |
| `btd-info.ps1` | `.btd` header dump | |
| `zone-info.ps1` / `show-zone.ps1` / `add-zone.ps1` | Menu/zone registration (needs `_ShowInMenu_` + both `available*Build` flags) | used for every launch |
| `nodedump.py` | Dumps one geometry node's records for diffing | |
| `streamlayout.py` | Corpus check: multi-primitive nodes are stream-major | 4915/4915 shipped nodes |
| `disx.py` | Disassembles at a runtime address from `pentane.log` (capstone + pefile) | |
| `vtxf.py` | `vformat.vtxf` parser. **`vformatCRC` = `name_hash(format GUID)`** (FUN_0044aa40) | hash reproduces `0x63ae710f` and sumo's `0xea1acc4c` |
| `shdfmt.py` | Per `.shd` pass: declared vertex format vs the VS's DXBC input signature (records at 0xF8, 80 B: +0 vformatCRC, +0x10 `<id>.vp`, +0x18 `<id>.fp`) | |
| `mdmp.py` | Minimal minidump reader, rebased to the exe's load address | reads the live vertex-format registry from `_dumps\*.dmp` |

## Vertex format path (from Ghidra, 2026-10-02)
- `shaders/vformat.vtxf` is loaded by `FUN_004642f0` into a registry at `0x02170664`, with 52 B per format and the count at `0x0216f660`.
- At runtime the registry holds 41 entries: slot 0 is empty and the rest are the 40 formats from the file.
- The primitive loader `FUN_004553c0` stores `lookup(vformatCRC)` as a **byte**. An unknown CRC returns `0xbadddec1`, which truncates to slot `0xc1`. That slot is empty, so the per-mesh input layout (`FUN_0045c5c0` -> `FUN_0044d430` -> `ID3D11Device::CreateInputLayout`) is built from nothing.
- 1.0's `ea1acc4c` (sumo, monolith) has a declaration identical to 3.0's `63ae710f`. 12 of the 44 1.0 formats have an exact 3.0 twin (`vtxf.py`).
