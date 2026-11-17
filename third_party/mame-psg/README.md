# MAME NCR8496/PSSJ-3 PSG adaptation

`mame_psg.c` and `mame_psg.h` adapt the individually BSD-3-Clause licensed
MAME SN76496 implementation to a small C interface for Restunts. This directory
does not include or link the MAME device framework or any libvgm modules.

Upstream repository: https://github.com/mamedev/mame

Pinned commit: `59cb74156d13b7c133dfc552ca07ae5e09e7d780`

Sources:

- https://github.com/mamedev/mame/blob/59cb74156d13b7c133dfc552ca07ae5e09e7d780/src/devices/sound/sn76496.cpp
- https://github.com/mamedev/mame/blob/59cb74156d13b7c133dfc552ca07ae5e09e7d780/src/devices/sound/sn76496.h
- https://github.com/mamedev/mame/blob/59cb74156d13b7c133dfc552ca07ae5e09e7d780/docs/legal/BSD-3-Clause

Copyright holder: Nicola Salmoria. The upstream source credits contributions by
others, including Sebastien Chevalier, Lord Nightmare, Michael Zapf, PlgDavid,
Enik, Nemesis, Qbix, ValleyBell and NewRisingSun. Preserve the source notices and
the BSD-3-Clause text in `LICENSE` when redistributing this adaptation. Installed
packages put the license at `share/licenses/restunts/MAME-PSG-LICENSE`. Binary
distributions must reproduce the notices, conditions and disclaimer in their
accompanying materials. MAME as a whole is GPL; these particular upstream files
are explicitly BSD-3-Clause, as permitted by MAME's `COPYING` document.

Local adaptation, 2026-11-17:

- Keep only the NCR8496 and PSSJ-3 mono profiles. Their only difference here is
  output polarity. Both preserve MAME's NCR noise taps, feedback, mode-change
  reset rules, tone-zero behavior and tone-driven noise period.
- Translate the register and divided-clock logic to C using the project's
  platform-independent `legacy_*` types. The header depends on the project's
  `legacy.h`; there are no allocation, SDL, C++ or math-library dependencies.
- Remove the MAME device, stream, READY-pin timer, logging and save-state APIs.
  Register writes apply at the current render boundary; callers control timing
  by rendering the preceding samples before writing the next byte.
- Preserve the initial MAME divider phase: the first state update occurs after
  14 input clocks, then every 16 clocks. Tone and noise counters continue while
  muted, and register writes do not restart tone counters.
- Replace the host stream resampler with exact integer weighted-area averaging
  across divided-clock transitions. Input-clock and sample-rate fractions remain
  intact across calls, so splitting an output buffer cannot change the samples.
  This reduces transition aliasing without adding an artificial smoothing delay.
- Convert each channel's upstream unipolar output to bipolar output before
  mixing. Four maximum-volume channels fit in signed-16 PCM; the NCR profile
  inverts the final mix and PSSJ-3 leaves it uninverted. Periodic noise can still
  have a DC component because its duty cycle is asymmetric.
- Precompute the upstream gain-zero, 2 dB attenuation table, avoiding floating
  point and a runtime math-library dependency. Attenuation 15 is exact silence.

Initialization and reset reproduce MAME's power-up volumes (all maximum).
Callers should write the four mute bytes before playing if startup silence is
required. `mame_psg_initialize` rejects zero rates and unknown profiles.

The tests in `src/restunts/tests/test-tandy-psg.c` cover known tone periods,
latched and continuation writes, noise feedback and reset behavior, output
polarity, fractional-clock area averaging, and rendering in different chunks.
