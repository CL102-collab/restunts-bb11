# Archived Borland regression oracles

These executable files use the original game disassembly and Borland-built dump
wrappers as independent regression references. They must not be rebuilt with
Watcom or replaced while fixing the C port.

CI uses the current original-assembly `repldumo.exe` and `pixldumo.exe` from
the DOS build artifact. Both wrappers read the recording's frame rate before
initializing replay state. The archived executables below force 20 Hz and
remain historical references. Use the source builds for 10 Hz recordings and
when regenerating their published `.BIN` and `.PDO` outputs. Existing 10 Hz
caches from the archived executables must be replaced for every camera and
target; their format alone does not identify the incorrect simulation rate.

Both renderer references use incremental redraws and hash every frame,
including frame 0, with the `PIXLDUMP 2` format.

| File | Bytes | Purpose |
| --- | ---: | --- |
| `repldumo.exe` | 244284 | Per-frame game state (`.BIN`) |
| `pixldumo.exe` | 247895 | MurmurHash3_x86_32 framebuffer samples (`.PDO`) |

`SHA256SUMS` identifies the exact archived bytes. The initial references were
copied from `stunts/` before compiler/linker migration. The physics oracle is
unchanged by the Murmur32 migration.

The renderer oracle was deliberately rebuilt with Borland on 2026-09-13 to
replace MD5 with MurmurHash3_x86_32, seed 0. Its pre-change rebuild matched the
previous archived executable byte for byte. The source fingerprints, tool hashes,
and regression results in [pixldumo-provenance.json](pixldumo-provenance.json)
describe that historical sampled, full-redraw build.

Commit `b900ca1b` on 2026-09-15 replaced it with the current incremental renderer
and updated `SHA256SUMS`. The provenance record identifies this replacement
separately; its earlier build inputs and validation results remain historical.
The C port's legacy memory model uses the incremental Borland oracle's retained
image size of `0x39E6` paragraphs; see [renderer parity](../../../docs/renderer-parity.md).

From the repository root, verify the archives and prepare both Borland
references for local comparisons on Linux:

```sh
(cd tools/oracles/borland && sha256sum --check SHA256SUMS)
cp tools/oracles/borland/repldumo.exe tools/oracles/borland/pixldumo.exe stunts/
```

Build the candidate `repldump` and `pixldump` executables using the
[repository build instructions](../../../readme.md#how-to-build). For local
comparisons against the archived renderer, use `pixelcheck.sh` with `rebuild=false`
after copying the references above. `rebuild=true` builds both renderer tools,
replacing `stunts/pixldumo.exe` with the Watcom source build.
`validate-toolchain.py` also takes `pixldumo.exe` from `--candidate-directory`
(default `stunts/`) while using the archived physics oracle. Source builds leave
the files in this archive unchanged.

Compare `.BIN` against ported `.BNI`, and `.PDO` against ported `.PDD`.
Current renderer dumps start with `PIXLDUMP 2` and CRLF, followed by one row
for every frame from frame 0 through the replay's final frame. Framebuffer rows
contain a decimal frame number, one space, eight lowercase
hexadecimal hash digits (most significant first), and CRLF. Camera and target
options must match on both sides. Headerless full-redraw outputs from earlier
renderer versions, five-frame sampled dumps, and old MD5 files are incompatible
with current incremental comparisons; the shared regression runner rejects and
regenerates those caches.

See the [shared regression runner](../../scripts/dumpsrv/README.md) for running
and merging replay comparisons. CI imports published physics and renderer caches
and uses the source-built original-assembly dumpers when regeneration is
needed. A deliberate oracle change requires provenance and regression review.
