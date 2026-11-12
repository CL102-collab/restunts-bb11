# puff raw DEFLATE decoder

`contrib/puff/puff.c` and `puff.h` from zlib **v1.3.1**:

- https://github.com/madler/zlib/tree/v1.3.1/contrib/puff
- https://github.com/madler/zlib/blob/v1.3.1/LICENSE

Copyright Mark Adler. The license notice in `puff.h` governs these sources;
`LICENSE` preserves both the puff notice and the upstream zlib license. The SDL3 car ZIP reader uses
this small decoder for raw DEFLATE members. It does not depend on a system zlib.

Restunts alters `fixed()` in `puff.c` to build its Huffman tables in automatic
storage on each call. This removes the upstream lazy initialization race when
multiple file readers inflate concurrently. The alteration is also marked in
`puff.c`; `puff.h` is unchanged. Other upstream formatting is preserved, with CRLF line endings for the repository.

Upstream source SHA-256 checksums before this alteration:

- `puff.c`: `5b9d75aeb5baf3575415bc6ade3f2a02e50b6b971b3f8b4fda2b03543bc6e52f`
- `puff.h`: `969b7be2a930db0cdcb19b0e5b29ae6741f5a8f663b6dba6d647e12ec60cfa8e`
