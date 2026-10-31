package org.restunts.genghost;

import android.content.Intent;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;

/** Validated options for one headless genghost search. */
public final class GenerationRequest {
    public static final int DEFAULT_SECONDS = 1800;
    public static final int DEFAULT_CORES = 1;
    public static final int MAX_CORES = 256;
    public static final int SECONDS_PER_MINUTE = 60;
    public static final int MAX_SECONDS = 86400;
    public static final int MAX_MINUTES = MAX_SECONDS / SECONDS_PER_MINUTE;
    public static final String DEFAULT_STRATEGY = "legacy";
    public static final String MODE_GENERATE = "generate";
    public static final String MODE_FINE_TUNE = "fine-tune";
    public static final String MODE_GEARS_ONLY = "gears-only";
    static final int REPLAY_HEADER_BYTES = 26;
    static final int REPLAY_CAR_OFFSET = 0;
    static final int REPLAY_CAR_BYTES = 4;
    static final int REPLAY_TRANSMISSION_OFFSET = 5;
    static final int REPLAY_AUTOMATIC_TRANSMISSION = 1;
    static final int REPLAY_OPPONENT_OFFSET = 6;
    static final int REPLAY_RATE_OFFSET = 22;
    static final int REPLAY_FRAME_COUNT_OFFSET = 24;
    static final int REPLAY_FRAMES_PER_SECOND = 20;
    static final int REPLAY_MAX_FRAMES = 12000;
    static final int EXTENDED_REPLAY_MAX_FRAMES = 120000;
    static final int EXTENDED_HEADER_BYTES = 64;
    static final int EXTENDED_MIN_VERSION = 11;
    static final int EXTENDED_HEADER_SIZE_OFFSET = 10;
    static final int EXTENDED_FLAGS_OFFSET = 12;
    static final int EXTENDED_TRACK_SIZE_OFFSET = 16;
    static final int EXTENDED_FRAME_COUNT_OFFSET = 20;
    static final int EXTENDED_RATE_OFFSET = 24;
    static final int EXTENDED_RULES_OFFSET = 26;
    static final int EXTENDED_CAR_OFFSET = 28;
    static final int EXTENDED_TRANSMISSION_OFFSET = 33;
    static final int EXTENDED_OPPONENT_OFFSET = 34;
    static final int EXTENDED_OPPONENT_TRANSMISSION_OFFSET = 40;
    static final int EXTENDED_RESERVED_OFFSET = 50;
    static final int EXTENDED_RULES_MASK = 0x7f;
    static final int RULE_POWER_GEAR = 0x01;
    static final int RULE_COLLISION = 0x02;
    static final int RULE_LEFT_BIAS = 0x04;
    static final int RULE_DUAL_WAY = 0x08;
    static final int RULE_CHICANE_GRASS = 0x10;
    static final int RULE_AMIGA_GRASS = 0x20;
    static final int RULE_OWOOT = 0x40;
    static final int REPLAY_INPUT_MASK = 63;
    static final int TRACK_HEADER_BYTES = 32;
    static final int TRACK_MIN_VERSION = 3;
    static final int TRACK_VERSION_OFFSET = 8;
    static final int TRACK_HEADER_SIZE_OFFSET = 10;
    static final int TRACK_HORIZON_OFFSET = 12;
    static final int TRACK_WIDTH_OFFSET = 16;
    static final int TRACK_HEIGHT_OFFSET = 18;
    static final int TRACK_NOTES_OFFSET = 20;
    static final int TRACK_OBJECT_COUNT_OFFSET = 24;
    static final int TRACK_PAYLOAD_OFFSET = 28;
    static final int TRACK_MAX_DIMENSION = 90;
    static final int TRACK_MAX_NOTES = 8000;
    static final int TRACK_OBJECT_BYTES = 16;
    static final int TRACK_CELL_BYTES = 2;
    static final int TRACK_HORIZONS = 5;
    static final int LEGACY_TRACK_BYTES = 1802;
    static final int TRACK_MAX_BYTES = TRACK_HEADER_BYTES
        + (TRACK_MAX_DIMENSION + 1) * (TRACK_MAX_DIMENSION + 1)
        + TRACK_MAX_DIMENSION * TRACK_MAX_DIMENSION * (TRACK_CELL_BYTES + TRACK_OBJECT_BYTES)
        + TRACK_MAX_NOTES;
    static final int EXTENDED_REPLAY_MAX_BYTES = EXTENDED_HEADER_BYTES + TRACK_MAX_BYTES
        + EXTENDED_REPLAY_MAX_FRAMES;
    private static final byte[] EXTENDED_MAGIC = {'C', 'S', 'R', 'P', 'L', '\r', '\n', 26};
    private static final byte[] TRACK_MAGIC = {'C', 'S', 'T', 'R', 'K', 26, '\r', '\n'};
    static final long REPLAY_INPUT_OFFSET = 1828L;
    static final int REPLAY_FRAME_MASK = 0xffff;
    static final int BYTE_MASK = 0xff;
    static final int BYTE_BITS = 8;
    public final String trackPath;
    public final String car;
    public final int seconds;
    public final int cores;
    public final String strategy;
    public final String mode;
    public final boolean fineTune;
    public final boolean fineTuneGearsOnly;
    public final String[] physicsFlags;

    public GenerationRequest(String trackPath, String car, int seconds, int cores, String strategy,
            String mode, String[] physicsFlags) {
        this.trackPath = trackPath == null ? "" : trackPath.trim();
        this.car = car == null ? "" : car.trim().toUpperCase(Locale.ROOT);
        this.seconds = seconds;
        this.cores = cores;
        this.strategy = strategy == null ? DEFAULT_STRATEGY : strategy;
        this.mode = mode == null ? MODE_GENERATE : mode;
        this.fineTuneGearsOnly = MODE_GEARS_ONLY.equals(this.mode);
        this.fineTune = MODE_FINE_TUNE.equals(this.mode) || fineTuneGearsOnly;
        this.physicsFlags = physicsFlags == null ? new String[0] : physicsFlags.clone();
    }

    void putInto(Intent intent) {
        intent.putExtra("track", trackPath);
        intent.putExtra("car", car);
        intent.putExtra("seconds", seconds);
        intent.putExtra("cores", cores);
        intent.putExtra("strategy", strategy);
        intent.putExtra("mode", mode);
        intent.putExtra("physics", physicsFlags);
    }

    static GenerationRequest from(Intent intent) {
        return new GenerationRequest(intent.getStringExtra("track"), intent.getStringExtra("car"),
            intent.getIntExtra("seconds", DEFAULT_SECONDS),
            intent.getIntExtra("cores", DEFAULT_CORES),
            intent.getStringExtra("strategy"), intent.getStringExtra("mode"),
            intent.getStringArrayExtra("physics"));
    }

    File validate(File game) throws IOException {
        if (!mode.equals(MODE_GENERATE) && !mode.equals(MODE_FINE_TUNE)
                && !mode.equals(MODE_GEARS_ONLY)) {
            throw new IOException("Choose one search mode.");
        }
        if (!fineTune && !CarResources.validCode(car)) {
            throw new IOException("Use a four-character car code, such as COUN or DIA3.");
        }
        if (seconds < 1 || seconds > MAX_SECONDS) {
            throw new IOException("Choose a search budget between 1 second and 24 hours.");
        }
        if (cores < 0 || cores > MAX_CORES) {
            throw new IOException("Choose between 1 and " + MAX_CORES
                + " cores, or 0 for all available cores.");
        }
        if (!strategy.equals("legacy") && !strategy.equals("state")) {
            throw new IOException("Unknown search method.");
        }
        Set<String> options = new HashSet<>();
        for (String flag : physicsFlags) {
            if (flag == null || !(flag.equals("--owoot") || flag.equals("--amigagrass")
                    || flag.matches("--(pg|lc|lcb|dws|cfg|owoot|amigagrass):(on|off)"))) {
                throw new IOException("Unknown physics option.");
            }
            String name = flag.split(":", 2)[0];
            if (!options.add(name)) {
                throw new IOException("A physics option was supplied more than once.");
            }
        }
        if (trackPath.isEmpty()) {
            throw new IOException("Choose a track or replay first.");
        }
        File track = new File(trackPath);
        if (!track.isAbsolute()) {
            track = new File(game, trackPath);
        }
        track = track.getCanonicalFile();
        if (!track.getPath().startsWith(game.getCanonicalPath() + File.separator)
                || !track.isFile() || !track.canRead()) {
            throw new IOException("The track must be a readable file in your game folder.");
        }
        String name = track.getName().toLowerCase(Locale.ROOT);
        if (!name.endsWith(".trk") && !name.endsWith(".rpl")
                && !name.endsWith(".cstrk") && !name.endsWith(".csrpl")) {
            throw new IOException("Choose a .TRK/.CSTRK track or .RPL/.CSRPL replay.");
        }
        if (fineTune) {
            if (!name.endsWith(".rpl") && !name.endsWith(".csrpl")) {
                throw new IOException("Fine-tuning requires a completed .RPL or .CSRPL replay.");
            }
            resolvedCar(track);
        } else if (name.endsWith(".cstrk")) {
            byte[] bytes = readBytes(track);
            validateTrack(bytes, 0, bytes.length);
        } else if (name.endsWith(".trk")) {
            if (track.length() != LEGACY_TRACK_BYTES) {
                throw new IOException("A legacy .TRK track must contain 1802 bytes.");
            }
        } else {
            readReplay(track);
        }
        return track;
    }

    /** Fine-tuning always uses the replay car, independently of the car selector. */
    String resolvedCar(File track) throws IOException {
        if (!fineTune) {
            return car;
        }
        ReplayInfo replay = readReplay(track);
        if (!CarResources.validCode(replay.car) || replay.opponent != 0 || replay.frames == 0
                || (!replay.extended && replay.rate != REPLAY_FRAMES_PER_SECOND)) {
            throw new IOException("Fine-tuning requires a complete solo .CSRPL or 20 Hz .RPL "
                + "replay with a valid car code.");
        }
        return replay.car.toUpperCase(Locale.ROOT);
    }

    static final class ReplayInfo {
        boolean extended;
        String car;
        long frames;
        int rate;
        int opponent;
        int transmission;
        int rules;
    }

    static String outputExtension(File track) {
        String name = track.getName().toLowerCase(Locale.ROOT);
        return name.endsWith(".cstrk") || name.endsWith(".csrpl") ? ".csrpl" : ".rpl";
    }

    private static byte[] readBytes(File file) throws IOException {
        long size = file.length();
        if (size <= 0 || size > EXTENDED_REPLAY_MAX_BYTES) {
            throw new IOException("The track or replay has an unsupported size.");
        }
        byte[] bytes = new byte[(int)size];
        try (FileInputStream input = new FileInputStream(file)) {
            int total = 0;
            while (total < bytes.length) {
                int count = input.read(bytes, total, bytes.length - total);
                if (count < 0) {
                    throw new IOException("The track or replay is incomplete.");
                }
                total += count;
            }
            if (input.read() != -1) {
                throw new IOException("The track or replay changed while reading.");
            }
        }
        return bytes;
    }

    private static int word(byte[] bytes, int offset) {
        return (bytes[offset] & BYTE_MASK) | ((bytes[offset + 1] & BYTE_MASK) << BYTE_BITS);
    }

    private static long dword(byte[] bytes, int offset) {
        return (long)word(bytes, offset) | ((long)word(bytes, offset + 2) << (2 * BYTE_BITS));
    }

    private static boolean signature(byte[] bytes, int offset, byte[] magic) {
        if (offset < 0 || bytes.length - offset < magic.length) {
            return false;
        }
        return Arrays.equals(Arrays.copyOfRange(bytes, offset, offset + magic.length), magic);
    }

    private static void validateTrack(byte[] bytes, int offset, int size) throws IOException {
        if (size < TRACK_HEADER_BYTES || !signature(bytes, offset, TRACK_MAGIC)
                || word(bytes, offset + TRACK_VERSION_OFFSET) < TRACK_MIN_VERSION
                || word(bytes, offset + TRACK_HEADER_SIZE_OFFSET) != TRACK_HEADER_BYTES
                || dword(bytes, offset + TRACK_HORIZON_OFFSET) >= TRACK_HORIZONS) {
            throw new IOException("The embedded .CSTRK track header is invalid.");
        }
        int width = word(bytes, offset + TRACK_WIDTH_OFFSET);
        int height = word(bytes, offset + TRACK_HEIGHT_OFFSET);
        long notes = dword(bytes, offset + TRACK_NOTES_OFFSET);
        long objects = dword(bytes, offset + TRACK_OBJECT_COUNT_OFFSET);
        long payload = dword(bytes, offset + TRACK_PAYLOAD_OFFSET);
        if (width < 1 || width > TRACK_MAX_DIMENSION || height < 1 || height > TRACK_MAX_DIMENSION
                || notes > TRACK_MAX_NOTES || objects > width * height
                || payload != (long)(width + 1) * (height + 1)
                    + (long)width * height * TRACK_CELL_BYTES + objects * TRACK_OBJECT_BYTES + notes
                || size != TRACK_HEADER_BYTES + payload) {
            throw new IOException("The .CSTRK track size does not match its layout.");
        }
        // Native genghost performs full terrain, placement and route admission before simulation.
    }

    static ReplayInfo readReplay(File file) throws IOException {
        byte[] bytes = readBytes(file);
        ReplayInfo replay = new ReplayInfo();
        int inputOffset;
        if (signature(bytes, 0, EXTENDED_MAGIC)) {
            if (bytes.length < EXTENDED_HEADER_BYTES
                    || word(bytes, TRACK_VERSION_OFFSET) < EXTENDED_MIN_VERSION
                    || word(bytes, EXTENDED_HEADER_SIZE_OFFSET) != EXTENDED_HEADER_BYTES
                    || dword(bytes, EXTENDED_FLAGS_OFFSET) != 0
                    || (word(bytes, EXTENDED_RULES_OFFSET) & ~EXTENDED_RULES_MASK) != 0) {
                throw new IOException("The .CSRPL replay header is unsupported or incomplete.");
            }
            for (int index = EXTENDED_RESERVED_OFFSET; index < EXTENDED_HEADER_BYTES; index++) {
                if (bytes[index] != 0) {
                    throw new IOException("The .CSRPL replay contains unsupported header data.");
                }
            }
            long trackSize = dword(bytes, EXTENDED_TRACK_SIZE_OFFSET);
            replay.extended = true;
            replay.frames = dword(bytes, EXTENDED_FRAME_COUNT_OFFSET);
            replay.rate = word(bytes, EXTENDED_RATE_OFFSET);
            replay.rules = word(bytes, EXTENDED_RULES_OFFSET);
            replay.car = new String(bytes, EXTENDED_CAR_OFFSET, REPLAY_CAR_BYTES,
                StandardCharsets.US_ASCII);
            replay.transmission = bytes[EXTENDED_TRANSMISSION_OFFSET] & BYTE_MASK;
            replay.opponent = bytes[EXTENDED_OPPONENT_OFFSET] & BYTE_MASK;
            if (trackSize > TRACK_MAX_BYTES || replay.frames > EXTENDED_REPLAY_MAX_FRAMES
                    || trackSize + EXTENDED_HEADER_BYTES + replay.frames != bytes.length
                    || (bytes[EXTENDED_OPPONENT_TRANSMISSION_OFFSET] & BYTE_MASK)
                        > REPLAY_AUTOMATIC_TRANSMISSION) {
                throw new IOException("The .CSRPL replay length or metadata is invalid.");
            }
            inputOffset = EXTENDED_HEADER_BYTES + (int)trackSize;
            validateTrack(bytes, EXTENDED_HEADER_BYTES, (int)trackSize);
        } else {
            if (bytes.length < REPLAY_INPUT_OFFSET) {
                throw new IOException("The legacy replay is incomplete.");
            }
            replay.frames = word(bytes, REPLAY_FRAME_COUNT_OFFSET);
            replay.rate = word(bytes, REPLAY_RATE_OFFSET);
            replay.car = new String(bytes, REPLAY_CAR_OFFSET, REPLAY_CAR_BYTES,
                StandardCharsets.US_ASCII);
            replay.transmission = bytes[REPLAY_TRANSMISSION_OFFSET] & BYTE_MASK;
            replay.opponent = bytes[REPLAY_OPPONENT_OFFSET] & BYTE_MASK;
            if (replay.frames > REPLAY_MAX_FRAMES
                    || REPLAY_INPUT_OFFSET + replay.frames != bytes.length) {
                throw new IOException("The legacy replay length is invalid.");
            }
            inputOffset = (int)REPLAY_INPUT_OFFSET;
        }
        if ((replay.rate != REPLAY_FRAMES_PER_SECOND
                && replay.rate != REPLAY_FRAMES_PER_SECOND / 2)
                || replay.transmission > REPLAY_AUTOMATIC_TRANSMISSION || replay.opponent > 6) {
            throw new IOException("The replay frame rate, transmission or opponent is invalid.");
        }
        for (int index = inputOffset; index < bytes.length; index++) {
            if ((bytes[index] & ~REPLAY_INPUT_MASK) != 0) {
                throw new IOException("The replay contains unsupported driving inputs.");
            }
        }
        return replay;
    }

    List<String> arguments(File executable, File game, File track, File output) {
        List<String> command = new ArrayList<>();
        command.add(executable.getAbsolutePath());
        command.add("--data-dir");
        command.add(game.getAbsolutePath());
        command.add(track.getAbsolutePath());
        if (!fineTune) {
            command.add(car);
        }
        command.add("--seconds");
        command.add(Integer.toString(seconds));
        command.add("--cores");
        command.add(Integer.toString(cores));
        command.add("--strategy");
        command.add(strategy);
        if (fineTune) {
            command.add(fineTuneGearsOnly ? "--fine-tune-gears-only" : "--fine-tune");
        }
        command.add("--output");
        command.add(output.getAbsolutePath());
        for (String flag : physicsFlags) {
            command.add(flag);
        }
        return command;
    }
}
