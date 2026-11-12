# HyperVision car meshes

SDL3 builds can load optional `ST<car ID>.H3D` companions alongside the
original `ST<car ID>.3SH` or `.P3S` model. HyperVision uses the companion's
available `car0`, `car1` and `car2` meshes for the showroom and racing.
Each mesh supports up to 65,535 vertices and 65,535 primitives, with signed
16-bit coordinate storage and the original material palette.

Turning HyperVision off selects the original model immediately. Existing cars
work without a companion; a missing or invalid companion also uses the original
meshes. Both renderers display geometry at its stored size; no companion
metadata changes a model's dimensions. Classic meshes retain their 255-vertex
format. Ports without HyperVision ignore `.H3D` files.

Put the companion in the same game-data or `cars/` locations as the other car
resources. It follows the normal loose-file, executable-directory and bundled
asset search order. `.H3D` is a separate resource family: it cannot substitute
for a missing classic `.3SH` or `.P3S` file.
If a higher-priority directory overrides the classic model, companions from
lower-priority directories are ignored. Place an enhanced override alongside
that replacement to enable its HyperVision mesh.

`STMCMU.H3D` supplies detailed McMurtry exterior meshes imported from the
supplied high-resolution GLB. The body retains source-derived triangles,
simplified to the companion's vertex and primitive limits; materials use the
native palette and all seven paints. Source wheel assemblies are replaced by
the native animated wheels. Offline generation bakes a uniform size factor
of 125/168 into both the classic and HyperVision McMurtry assets, making
all three models approximately 25.6% smaller. The factor follows its real
height ratio to the stock Audi Sport Quattro model. Classic integer geometry
and wheel controls are authored at the smaller size before quantization;
the companion retains higher body precision. The game applies no per-car
presentation scaling. Physics resources retain their original values.
Regenerate the package with:

```sh
python3 tools/scripts/generate-mcmurtry-speirling.py --install stunts
python3 tools/scripts/generate-mcmurtry-speirling.py --check --install stunts
```

Regeneration uses the compact transformed source mesh and precomputed detail
levels in `tools/scripts/cars/mcmurtry_hypervision_source.json.gz` through
`tools/scripts/mcmurtry_hypervision.py`; it needs only Python's standard library.
The original GLB and textures are not required or bundled. See the
[McMurtry source notes](mcmurtry-speirling.md#detailed-hypervision-companion)
for attribution, optional offline-import dependencies and coordinate precision.

## Container format, version 2

All numeric fields are little-endian, with no alignment padding. The file
starts with this eight-byte header:

| Offset | Type | Meaning |
| --- | --- | --- |
| 0 | 4 bytes | `H3D` followed by a zero byte |
| 4 | unsigned 16-bit | Version, 2 |
| 6 | unsigned 16-bit | Number of directory entries, 1–3 |

There are no presentation-scale fields. Coordinates describe the model's
final size. Version 3 companions are unsupported and fall back to the classic
model; regenerate them as version 2 with any desired sizing baked into the
geometry. Decoding fixed-point body coordinates changes their representation,
not the model's dimensions.

Each 12-byte directory entry contains a four-byte name (`car0`, `car1` or
`car2`), an unsigned 32-bit absolute file offset and an unsigned 32-bit byte
length. Names must be unique. Shape payloads must lie beyond the directory,
within the file, and must not overlap. The current loader accepts files up
to 64 MiB. An omitted mesh uses its classic counterpart.

Each shape payload starts with an eight-byte header:

| Offset | Type | Meaning |
| --- | --- | --- |
| 0 | unsigned 16-bit | Vertex count, 1–65,535 |
| 2 | unsigned 16-bit | Primitive count, 1–65,535 |
| 4 | unsigned 16-bit | Paint count, 1–255 |
| 6 | unsigned 16-bit | `fraction_bits`, 0–8 |

The header is followed by `vertex_count` signed 16-bit XYZ triples, then
`primitive_count` unsigned 32-bit visibility masks and the same number of
unsigned 32-bit front-facing masks. Their meanings match the classic format.
The `fraction_bits` field sets the per-shape fractional coordinate scale.
The first eight bounding vertices and following 24 animated wheel controls
always retain whole native units. For vertices at index 32 and above, divide
stored coordinates by `1 << fraction_bits` to obtain model coordinates. Zero
fraction bits use whole model units; four give 1/16-unit body coordinates
and eight give 1/256-unit coordinates. The generated McMurtry uses five bits
for its 20× showroom body and eight for both racing bodies: stored steps are
44 / (20 × 32) = 0.06875 mm and 44 / 256 = 0.171875 mm respectively under its
authoring unit conversion. Bounds and wheel controls remain integers.

The loader accepts container version 2 only. It rejects the complete companion
if the version is unsupported, the fraction count exceeds eight, or any other
header, directory or shape validation fails. Rejected companions use the
classic model.

Primitive records contain a one-byte type, one-byte flags, one material byte
per paint job, then vertex indices. Types 1–12 retain their classic meanings
and index counts: point 1, line 2, polygon 3–10, sphere 2 and wheel 6. Flags
retain the two-sided and attached bits; other bits are invalid. Materials must
be in the native palette range 0–128.

**Index width depends on the shape's vertex count:** use one-byte indices for
up to 255 vertices and unsigned 16-bit indices above 255. Every index must be
less than the vertex count. The primitive stream must consume the payload
exactly; it has no end marker or trailing padding.

Car meshes must contain the first eight bounding vertices followed by the
original 24 wheel-control vertices. Wheel geometry and paint count must match
the corresponding classic mesh. The cached front controls may have both
horizontal axes reversed by the classic engine's neutral-wheel reset; the
loader accepts that equivalent representation and copies the current controls.
The engine copies steering and suspension
changes into the companion before rendering. Physical tyre contacts and
collision dimensions come from the car's physics resource, independently of
any offline visual resizing. Player and opponent meshes have independent
writable storage.

The loader validates the complete companion before publishing any replacement.
HyperVision decodes wide indices and allocates transform scratch for larger
meshes. Ordinary meshes retain the existing inline transform storage.
