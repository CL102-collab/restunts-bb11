# stb_vorbis

Vendored Ogg Vorbis decoder from [nothings/stb](https://github.com/nothings/stb).

- Upstream revision: `2c980bb59875b0d32144a71867fbdebb2f77cd20`.
- Decoder version: 1.22.
- [Decoder source](https://github.com/nothings/stb/blob/2c980bb59875b0d32144a71867fbdebb2f77cd20/stb_vorbis.c).
- [License](https://github.com/nothings/stb/blob/2c980bb59875b0d32144a71867fbdebb2f77cd20/LICENSE).
- Upstream decoder SHA-256: `4c7cb2ff1f7011e9d67950446b7eb9ca044f2e464d76bfbb0b84dd2e23e65636`.

Restunts patches memory seeks to compare the offset with the buffer length
before forming a pointer. The bundled license offers the MIT license or
public-domain dedication. Restunts redistributes it under the MIT option.

All SDL3 builds compile the decoder into `restunts_game`. The unused push-data
API is disabled with `STB_VORBIS_NO_PUSHDATA_API`. The application archive therefore
also contains the decoder in the browser relink package. No external Vorbis
library is required.
