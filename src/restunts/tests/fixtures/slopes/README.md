# Sloped replay regressions

`half-pipe-uphill.csrpl` is the original user-provided PMIN recording, with its
embedded extended track and all 1,178 input frames preserved. It contains no
original game assets or track notes. At 20 frames per second, the old affine
collision helper caused a false body collision at frame 1097 (54.85 seconds).
A nested collision lookup replaced the shared car rotation matrix with a
track-plane rotation before the remaining body corners were transformed.

The fixture retains its recorded replay version v7. The engine regression
updates only that header field in memory to the current replay revision before
loading it, so the original controls can exercise the corrected physics. The
normal application still rejects incompatible older replay revisions.

SHA-256: `b62d5b04b2a23f0e538dc7e51c3b492e55aaf7a058a78983d16007f232a20d96`.

## Fan adhesion on consecutive road slopes

`fan-adhesion-slopes.csrpl` is the user-provided McMurtry recording, with its
embedded 20×20 test map and all 195 input frames preserved. It contains four
consecutive road slopes and no original game resources or track notes. The
engine test plays its controls with the actual McMurtry and Tesla resources,
then drives both cars down the same uninterrupted grade.

The old fan snap used an integer surface distance and a rounded normal after
extended contact had measured a precise support height. This introduced roughly
one world unit of vertical heave and repeated two- or three-unit pitch kicks.
The regression checks fixed-point body height above the grade and permits only
the normal one-angle-unit pose quantization. Its downhill pass starts aligned
on the slope so a legitimate high-speed launch from the sharp crest cannot
mask the contact behavior under test.

SHA-256: `f07a87f94d6cc477b7e7c5cfa3028ab6d18850933bb94f6b9fe96724d8d1e4bd`.
