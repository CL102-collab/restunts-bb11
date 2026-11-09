# HyperVision car meshes

SDL3 builds can load optional `ST<car ID>.H3D` companions alongside the
original `ST<car ID>.3SH` or `.P3S` model. HyperVision uses the companion's
available `car0`, `car1` and `car2` meshes for the showroom and racing.
Each mesh supports up to 65,535 vertices and 65,535 primitives, with signed
16-bit coordinate storage and the original material palette.

Turning HyperVision off selects the original model immediately. Existing cars
work without a companion; a missing or invalid companion also uses the original
meshes. Classic model bytes, their 255-vertex format, the classic renderer and
physics remain unchanged. Ports without HyperVision ignore `.H3D` files.

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
the existing native animated wheels. A version 3 uniform scale of 125/168
makes all three McMurtry meshes 25.6% smaller, matching its real height ratio
to the stock Audi Sport Quattro model without changing any proportions.
The classic fallback and physics retain
the existing model. Regenerate the package with:

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

## Container format, versions 2 and 3

All numeric fields are little-endian, with no alignment padding. The file
starts with this header (eight bytes in version 2, twelve in version 3):

| Offset | Type | Meaning |
| --- | --- | --- |
| 0 | 4 bytes | `H3D` followed by a zero byte |
| 4 | unsigned 16-bit | Version, 2 or 3 |
| 6 | unsigned 16-bit | Number of directory entries, 1–3 |
| 8 | unsigned 16-bit | Version 3 only: nonzero uniform-scale numerator |
| 10 | unsigned 16-bit | Version 3 only: nonzero uniform-scale denominator |

Version 2 has a uniform scale of 1. Version 3 applies numerator/denominator
to all decoded coordinates, including bounds and animated wheel controls.
The showroom scales about `(0, 0, 0)`; racing meshes scale about `(0, -6, 0)`
to preserve the native tyre contact plane. This is a visual transform: physics
contacts, collision sizes and classic fallback geometry retain their existing
units. The scale never rewrites or requantizes shape payloads.

Body coordinates are scaled once at load time. After native wheel animation,
the first 32 precise coordinates are refreshed from the unscaled controls and
scaled once. Rendering, bounds culling, ground placement and shadows share
these precise coordinates. Version 3 supports fractional presentation even
when a shape has zero body fraction bits.

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
fraction bits use whole model units; four give 1/16-unit body coordinates,
about 2.75 mm per racing-model step for the McMurtry. The fractional scale
changes visual body precision while physics, bounds and wheel animation
continue to use the existing native units.

The loader accepts container versions 2 and 3. It rejects the complete companion
if the version is unsupported, a scale component is zero, the fraction count
exceeds eight, or scaled coordinates exceed signed 16-bit bounds. Rejected
companions use the classic model.

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
changes into the companion before rendering; physics always uses the classic
controls. Player and opponent meshes have independent writable storage.

The loader validates the complete companion before publishing any replacement.
HyperVision decodes wide indices and allocates transform scratch for larger
meshes. Ordinary meshes retain the existing inline transform storage.
