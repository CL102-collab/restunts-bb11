#!/usr/bin/env python3
"""Convert the game's KMS music resources to General MIDI files.

Run without arguments to write out/game-music. No third-party packages are
needed. --check verifies that existing exports match a fresh conversion.

The sequence decoder follows audio/audio_resources.c and audio/audio_sequence.c.
Songs are exported through one complete repeat, with loop markers and note tails
clipped at the repeat boundary. Instrument timbres are GM approximations;
the original AdLib/MT-32 patches cannot be represented by GM program numbers.
"""

import argparse
import hashlib
import json
from pathlib import Path
import struct


ROOT = Path(__file__).resolve().parents[2]
RESOURCE_HEADER = struct.Struct("<IH")
RESOURCE_OFFSET = struct.Struct("<I")
RESOURCE_ID_SIZE = 4
RESOURCE_ENTRY_SIZE = RESOURCE_ID_SIZE + RESOURCE_OFFSET.size
CHUNK_HEADER_SIZE = RESOURCE_OFFSET.size
SONG_INSTRUMENT_COUNT_OFFSET = 6
SONG_TRACK_ENTRY_SIZE = RESOURCE_ID_SIZE + 1
INSTRUMENT_PITCH_OFFSET = 16
INSTRUMENT_BANK = "ADSKIDMS.VCE"
OCTAVE_SEMITONES = 12

COMMAND_RETURN = 0xD9
COMMAND_STOP = 0xDA
COMMAND_RESTART = 0xDB
COMMAND_INSTRUMENT = 0xDC
COMMAND_TEMPO = 0xDD
COMMAND_VOLUME = 0xDE
COMMAND_CONTROL = 0xDF
COMMAND_NOTE_LIMIT = 0xE0
COMMAND_PRIORITY = 0xE1
COMMAND_LOOP_BEGIN = 0xE2
COMMAND_LOOP_END = 0xE3
COMMAND_VELOCITY = 0xE4
COMMAND_PITCH = 0xE5
COMMAND_CALL = 0xE6
COMMAND_TEXT = 0xE7
COMMAND_DRIVER_DATA = 0xE8
COMMAND_DRIVER_CHANNEL = 0xE9
COMMAND_RESERVED = 0xEA
BYTE_COMMANDS = {
    COMMAND_INSTRUMENT, COMMAND_TEMPO, COMMAND_VOLUME, COMMAND_NOTE_LIMIT,
    COMMAND_PRIORITY, COMMAND_LOOP_BEGIN, COMMAND_VELOCITY,
    COMMAND_DRIVER_CHANNEL, COMMAND_RESERVED,
}
LONG_LOOP_COUNT = 0xFF
MAX_SEQUENCE_EVENTS = 1_000_000
MAX_CALL_DEPTH = 4

MIDI_DATA_BITS = 7
MIDI_DATA_MAX = 0x7F
MIDI_STATUS_BIT = 0x80
MIDI_EVENT_MASK = 0xF0
MIDI_VLQ_MAX = 0x0FFFFFFF
MIDI_VLQ_BYTES = 4
MIDI_CHANNEL_COUNT = 16
MIDI_DRUM_CHANNEL = 9
MIDI_NOTE_OFF = 0x80
MIDI_NOTE_ON = 0x90
MIDI_CONTROL = 0xB0
MIDI_PROGRAM = 0xC0
MIDI_PITCH = 0xE0
MIDI_META = 0xFF
MIDI_TRACK_NAME = 0x03
MIDI_INSTRUMENT_NAME = 0x04
MIDI_MARKER = 0x06
MIDI_END_TRACK = 0x2F
MIDI_TEMPO = 0x51
MIDI_TEMPO_BYTES = 3
MIDI_VOLUME = 7
MIDI_SUSTAIN = 64
MIDI_ALL_NOTES_OFF = 123
MIDI_FORMAT = 1
MIDI_HEADER = struct.Struct(">HHH")
MIDI_CHUNK_SIZE = struct.Struct(">I")
MIDI_TICKS_PER_QUARTER = 24
TIMER_TICK_MICROSECONDS = 10_000
MICROSECONDS_PER_SECOND = 1_000_000
SEQUENCE_TIMER_STEP = 128
SEQUENCE_TEMPO_NUMERATOR = 32_000
DEFAULT_SEQUENCE_PERIOD = SEQUENCE_TIMER_STEP
GM_SYSTEM_ON = bytes.fromhex("F0 05 7E 7F 09 01 F7")

# Zero-based General MIDI programs. These are audible approximations of the
# named game voices, not claims that the original FM patches are GM patches.
GM_PROGRAMS = {
    "BASS": (38, "Synth Bass 1"),
    "LEAD": (81, "Lead 2 (sawtooth)"),
    "GUIT": (30, "Distortion Guitar"),
    "STRT": (27, "Electric Guitar (clean)"),
    "HRN1": (62, "SynthBrass 1"),
    "HRN2": (62, "SynthBrass 1"),
    "KEYS": (4, "Electric Piano 1"),
}
SONG_TITLES = {
    "titl": "Title", "slct": "Menu selection", "over": "Race end", "vict": "Victory",
}


def resources(data):
    """Read the game's shared resource directory, including nested songs."""
    if len(data) < RESOURCE_HEADER.size:
        raise ValueError("Truncated resource header")
    size, count = RESOURCE_HEADER.unpack_from(data)
    base = RESOURCE_HEADER.size + count * RESOURCE_ENTRY_SIZE
    if not base <= size <= len(data):
        raise ValueError("Invalid resource size or directory")
    offsets = RESOURCE_HEADER.size + count * RESOURCE_ID_SIZE
    entries = []
    for index in range(count):
        start = RESOURCE_HEADER.size + index * RESOURCE_ID_SIZE
        name = data[start:start + RESOURCE_ID_SIZE].decode("ascii").upper()
        offset = base + RESOURCE_OFFSET.unpack_from(
            data, offsets + index * RESOURCE_OFFSET.size
        )[0]
        if not base <= offset < size:
            raise ValueError(f"Invalid resource offset: {name}")
        entries.append((offset, name))
    entries.sort()
    result = {}
    for index, (offset, name) in enumerate(entries):
        end = entries[index + 1][0] if index + 1 < count else size
        if name in result or end <= offset:
            raise ValueError(f"Duplicate resource name or offset: {name}")
        result[name] = data[offset:end]
    return result


class Reader:
    def __init__(self, data, position=0):
        self.data = data
        self.position = position

    def take(self, size):
        end = self.position + size
        if end > len(self.data):
            raise ValueError(f"Truncated sequence at byte {self.position}")
        value = self.data[self.position:end]
        self.position = end
        return value

    def byte(self):
        return self.take(1)[0]

    def vlq(self):
        value = 0
        for _ in range(MIDI_VLQ_BYTES):
            byte = self.byte()
            value = (value << MIDI_DATA_BITS) | (byte & MIDI_DATA_MAX)
            if not byte & MIDI_STATUS_BIT:
                return value
        raise ValueError("Variable-length quantity exceeds four bytes")


def sequence_reader(data):
    if len(data) < CHUNK_HEADER_SIZE:
        raise ValueError("Truncated sequence chunk")
    size = RESOURCE_OFFSET.unpack_from(data)[0]
    if not CHUNK_HEADER_SIZE < size <= len(data):
        raise ValueError("Invalid sequence chunk size")
    return Reader(data[:size], CHUNK_HEADER_SIZE)


def read_event(reader):
    delay = reader.vlq()
    command = reader.byte()
    if command < COMMAND_RETURN:
        velocity = reader.byte() if command > MIDI_STATUS_BIT else None
        arguments = (velocity, reader.vlq())
    elif command in BYTE_COMMANDS:
        arguments = (reader.byte(),)
    elif command in (COMMAND_CONTROL, COMMAND_PITCH):
        arguments = tuple(reader.take(2))
    elif command == COMMAND_CALL:
        arguments = (reader.byte(), reader.take(RESOURCE_ID_SIZE).decode("ascii").upper())
    elif command in (COMMAND_TEXT, COMMAND_DRIVER_DATA):
        arguments = (reader.take(reader.byte()),)
    elif command in (COMMAND_RETURN, COMMAND_STOP, COMMAND_RESTART, COMMAND_LOOP_END):
        arguments = ()
    else:
        raise ValueError(f"Unknown sequence command: {command:#x}")
    return delay, command, arguments


def sequence_events(chunks, track):
    """Expand calls and finite loops; end after one long loop or restart."""
    reader = sequence_reader(chunks[track])
    calls = []
    loops = []
    tick = 0
    first = True
    for _ in range(MAX_SEQUENCE_EVENTS):
        delay, command, arguments = read_event(reader)
        # audio_init_channel_range starts immediately, ignoring the first delay.
        tick += 0 if first else delay
        first = False
        if command == COMMAND_CALL:
            if len(calls) >= MAX_CALL_DEPTH:
                raise ValueError("Sequence call stack overflow")
            calls.append(reader)
            reader = sequence_reader(chunks[arguments[1]])
        elif command == COMMAND_RETURN and calls:
            reader = calls.pop()
        elif command in (COMMAND_RETURN, COMMAND_STOP, COMMAND_RESTART):
            yield tick, command, arguments
            return
        elif command == COMMAND_LOOP_BEGIN:
            count = arguments[0] or LONG_LOOP_COUNT + 1
            loops.append([reader.data, reader.position, count, tick,
                          arguments[0] == LONG_LOOP_COUNT])
            yield tick, command, arguments
        elif command == COMMAND_LOOP_END:
            if not loops:
                continue
            data, position, count, start, long_loop = loops[-1]
            if long_loop:
                yield tick, command, (start,)
                return
            # The original rewinds even on the final decrement, hence N+1
            # passes for a finite count N. Count zero wraps to 257 passes.
            loops[-1][2] = count - 1
            reader = Reader(data, position)
            if count == 1:
                loops.pop()
        else:
            yield tick, command, arguments
    raise ValueError("Sequence did not terminate within the event limit")


def vlq(value):
    if not 0 <= value <= MIDI_VLQ_MAX:
        raise ValueError(f"Invalid MIDI variable-length quantity: {value}")
    output = [value & MIDI_DATA_MAX]
    value >>= MIDI_DATA_BITS
    while value:
        output.insert(0, (value & MIDI_DATA_MAX) | MIDI_STATUS_BIT)
        value >>= MIDI_DATA_BITS
    return bytes(output)


def meta(kind, payload):
    return bytes((MIDI_META, kind)) + vlq(len(payload)) + payload


def midi_message(status, *values):
    if any(not 0 <= value <= MIDI_DATA_MAX for value in values):
        raise ValueError(f"Invalid MIDI data bytes: {values}")
    return bytes((status, *values))


def midi_chunk(kind, data):
    return kind + MIDI_CHUNK_SIZE.pack(len(data)) + data


def midi_track(events, end):
    data = bytearray()
    previous = 0
    # Python's stable sort preserves command ordering at the same tick.
    for tick, message in sorted(events, key=lambda event: event[0]):
        data.extend(vlq(tick - previous))
        data.extend(message)
        previous = tick
    data.extend(vlq(max(previous, end) - previous))
    data.extend(meta(MIDI_END_TRACK, b""))
    return midi_chunk(b"MTrk", data)


def tempo_message(period):
    microseconds = round(
        MIDI_TICKS_PER_QUARTER * period * TIMER_TICK_MICROSECONDS / SEQUENCE_TIMER_STEP
    )
    return meta(MIDI_TEMPO, microseconds.to_bytes(MIDI_TEMPO_BYTES, "big"))


def convert_track(chunks, track, instruments, bank, channel):
    events = []
    tempos = []
    notes = []
    name = track
    instrument = None
    velocity = MIDI_DATA_MAX
    volume = MIDI_DATA_MAX
    end = 0
    loop_start = None
    used_instruments = set()
    for tick, command, arguments in sequence_events(chunks, track):
        end = tick
        if command < COMMAND_RETURN:
            if instrument is None:
                raise ValueError(f"{track}: note before instrument selection")
            note_velocity, duration = arguments
            note_velocity = velocity if note_velocity is None else note_velocity
            pitch = command & MIDI_DATA_MAX
            if instrument == "DRUM":
                note_channel = MIDI_DRUM_CHANNEL
                pitch += OCTAVE_SEMITONES
                # Several KMS drum tracks share GM channel 10. Fold their
                # separate channel volumes into velocities to avoid conflicts.
                note_velocity = round(note_velocity * volume / MIDI_DATA_MAX)
            else:
                note_channel = channel
                pitch += struct.unpack_from("b", bank[instrument], INSTRUMENT_PITCH_OFFSET)[0]
                pitch += OCTAVE_SEMITONES
            if duration == 0:
                raise ValueError("Unbounded note duration is not supported")
            notes.append((tick, tick + duration, note_channel, pitch, note_velocity))
        elif command == COMMAND_TEXT:
            text = arguments[0].rstrip(b"\0").decode("ascii")
            if text:
                name = text
                events.append((tick, meta(MIDI_TRACK_NAME, text.encode("ascii"))))
        elif command == COMMAND_INSTRUMENT:
            instrument = instruments[arguments[0]]
            used_instruments.add(instrument)
            events.append((tick, meta(MIDI_INSTRUMENT_NAME, instrument.encode("ascii"))))
            if instrument != "DRUM":
                program, _ = GM_PROGRAMS[instrument]
                events.append((tick, midi_message(MIDI_PROGRAM | channel, program)))
                events.append((tick, midi_message(MIDI_CONTROL | channel, MIDI_VOLUME, volume)))
        elif command == COMMAND_TEMPO:
            if arguments[0] == 0:
                raise ValueError("Zero sequence tempo")
            period = SEQUENCE_TEMPO_NUMERATOR // arguments[0]
            tempos.append((tick, period))
        elif command in (COMMAND_VOLUME, COMMAND_CONTROL):
            control, value = ((MIDI_VOLUME, arguments[0]) if command == COMMAND_VOLUME
                              else arguments)
            if control == MIDI_VOLUME:
                volume = value
            if instrument != "DRUM":
                events.append((tick, midi_message(MIDI_CONTROL | channel, control, value)))
            elif control != MIDI_VOLUME:
                raise ValueError(f"Unsupported shared percussion controller: {control}")
        elif command == COMMAND_VELOCITY:
            velocity = arguments[0]
        elif command == COMMAND_PITCH:
            events.append((tick, midi_message(MIDI_PITCH | channel, *arguments)))
        elif command == COMMAND_LOOP_END:
            loop_start = arguments[0]
        elif command == COMMAND_RESTART:
            loop_start = 0
        elif command in (COMMAND_DRIVER_DATA, COMMAND_DRIVER_CHANNEL):
            raise ValueError(f"Driver-specific command cannot be converted to GM: {command:#x}")

    note_events = []
    for start, stop, note_channel, pitch, note_velocity in notes:
        if start >= end:
            continue
        stop = min(stop, end)
        note_events.append((start, midi_message(MIDI_NOTE_ON | note_channel, pitch, note_velocity)))
        note_events.append((stop, midi_message(MIDI_NOTE_OFF | note_channel, pitch, 0)))
    # End notes before retriggering the same key at the same tick.
    note_events.sort(key=lambda event: (event[0], event[1][0] & MIDI_EVENT_MASK))
    events.extend(note_events)
    events.append((end, midi_message(MIDI_CONTROL | channel, MIDI_SUSTAIN, 0)))
    if loop_start is not None:
        events.extend((
            (loop_start, meta(MIDI_MARKER, b"loopStart")),
            (end, meta(MIDI_MARKER, b"loopEnd")),
        ))
    return midi_track(events, end), tempos, {
        "resource": track, "name": name, "instruments": sorted(used_instruments),
        "notes": len(note_events) // 2, "end_tick": end, "loop_start_tick": loop_start,
    }


def song_header(chunks):
    reader = sequence_reader(chunks["HDR1"])
    reader.take(SONG_INSTRUMENT_COUNT_OFFSET - CHUNK_HEADER_SIZE)
    instruments = [reader.take(RESOURCE_ID_SIZE).decode("ascii").upper()
                   for _ in range(reader.byte())]
    tracks = []
    for _ in range(reader.byte()):
        reference = reader.take(SONG_TRACK_ENTRY_SIZE)
        tracks.append(reference[:RESOURCE_ID_SIZE].decode("ascii").upper())
    if not 0 < len(tracks) < MIDI_CHANNEL_COUNT:
        raise ValueError("Unsupported music channel count")
    return instruments, tracks


def convert_song(data, title, bank):
    chunks = resources(data)
    instruments, tracks = song_header(chunks)
    converted = []
    metadata = []
    tempos = {0: DEFAULT_SEQUENCE_PERIOD}
    channels = [channel for channel in range(MIDI_CHANNEL_COUNT) if channel != MIDI_DRUM_CHANNEL]
    for track, channel in zip(tracks, channels):
        output, changes, info = convert_track(chunks, track, instruments, bank, channel)
        converted.append(output)
        metadata.append(info)
        for tick, period in changes:
            if tick in tempos and tick != 0 and tempos[tick] != period:
                raise ValueError(f"Conflicting tempo changes at tick {tick}")
            tempos[tick] = period
    end = max(track["end_tick"] for track in metadata)
    conductor = [(0, meta(MIDI_TRACK_NAME, title.encode("ascii"))), (0, GM_SYSTEM_ON)]
    conductor.extend((tick, tempo_message(period)) for tick, period in sorted(tempos.items()))
    # Shared percussion channel initialization and cleanup belong in one track.
    conductor.append((0, midi_message(
        MIDI_CONTROL | MIDI_DRUM_CHANNEL, MIDI_VOLUME, MIDI_DATA_MAX
    )))
    conductor.append((end, midi_message(MIDI_CONTROL | MIDI_DRUM_CHANNEL, MIDI_ALL_NOTES_OFF, 0)))
    header = MIDI_HEADER.pack(MIDI_FORMAT, len(converted) + 1, MIDI_TICKS_PER_QUARTER)
    output = midi_chunk(b"MThd", header) + midi_track(conductor, end) + b"".join(converted)
    changes = sorted(tempos.items()) + [(end, None)]
    seconds = sum((next_tick - tick) * period * TIMER_TICK_MICROSECONDS
                  / SEQUENCE_TIMER_STEP / MICROSECONDS_PER_SECOND
                  for (tick, period), (next_tick, _) in zip(changes, changes[1:]))
    return output, {
        "title": title, "duration_seconds": seconds, "tracks": metadata,
        "tempo_changes": [{"tick": tick, "sequence_period": period}
                          for tick, period in sorted(tempos.items())],
    }


def digest(data):
    return hashlib.sha256(data).hexdigest()


def generate(directory):
    bank_source = (directory / INSTRUMENT_BANK).read_bytes()
    bank = resources(bank_source)
    sources = sorted(path for path in directory.iterdir() if path.suffix.upper() == ".KMS")
    if not sources:
        raise ValueError(f"No KMS music resources in {directory}")
    outputs = {}
    manifest = {
        "format": "Standard MIDI File type 1, General MIDI",
        "ticks_per_quarter": MIDI_TICKS_PER_QUARTER,
        "repeat_policy": "One complete long loop/restart; notes clipped at its boundary",
        "timing": "100 Hz sequencer; period = floor(32000 / tempo), step = 128",
        "instruments": "GM approximations; original AdLib/MT-32 timbres are not preserved",
        "pitch_mapping": "AdLib signed base pitch + 12; percussion uses MT-32/GM note + 12",
        "instrument_bank": INSTRUMENT_BANK,
        "instrument_bank_sha256": digest(bank_source),
        "gm_programs_zero_based": GM_PROGRAMS,
        "songs": [],
    }
    for source in sources:
        source_data = source.read_bytes()
        songs = resources(source_data)
        for resource_id, data in songs.items():
            title = SONG_TITLES.get(resource_id.lower(), resource_id)
            filename = source.stem.lower()
            if len(songs) > 1:
                filename += "-" + resource_id.lower()
            filename += ".mid"
            if filename in outputs:
                raise ValueError(f"Duplicate output filename: {filename}")
            output, info = convert_song(data, title, bank)
            outputs[filename] = output
            info.update({"source": source.name, "resource": resource_id, "file": filename,
                         "source_sha256": digest(source_data), "midi_sha256": digest(output)})
            manifest["songs"].append(info)
    outputs["manifest.json"] = (json.dumps(manifest, indent=2) + "\n").encode("utf-8")
    return outputs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-directory", type=Path, default=ROOT / "stunts")
    parser.add_argument("--output-directory", type=Path, default=ROOT / "out/game-music")
    parser.add_argument("--check", action="store_true", help="Verify outputs without writing")
    args = parser.parse_args()
    try:
        outputs = generate(args.game_directory)
        if not args.check:
            args.output_directory.mkdir(parents=True, exist_ok=True)
        for name, data in outputs.items():
            path = args.output_directory / name
            if args.check:
                if path.read_bytes() != data:
                    raise ValueError(f"Extraction differs: {path}")
            else:
                path.write_bytes(data)
        action = "Verified" if args.check else "Extracted"
        print(f"{action} {len(outputs) - 1} MIDI files and metadata in {args.output_directory}")
    except (OSError, ValueError, KeyError, IndexError, struct.error) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
