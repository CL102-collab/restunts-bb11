# Sloped half-pipe replay regression

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
