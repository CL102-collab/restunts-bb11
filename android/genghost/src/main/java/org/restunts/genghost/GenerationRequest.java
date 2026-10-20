package org.restunts.genghost;

import android.content.Intent;
import java.io.File;
import java.io.IOException;
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
    public static final String DEFAULT_STRATEGY = "legacy";
    public final String trackPath;
    public final String car;
    public final int seconds;
    public final int cores;
    public final String strategy;
    public final boolean fineTune;
    public final String transmission;
    public final String[] physicsFlags;

    public GenerationRequest(String trackPath, String car, int seconds, int cores, String strategy,
            boolean fineTune, String transmission, String[] physicsFlags) {
        this.trackPath = trackPath == null ? "" : trackPath.trim();
        this.car = car == null ? "" : car.trim().toUpperCase(Locale.ROOT);
        this.seconds = seconds;
        this.cores = cores;
        this.strategy = strategy == null ? DEFAULT_STRATEGY : strategy;
        this.fineTune = fineTune;
        this.transmission = transmission == null ? "source" : transmission;
        this.physicsFlags = physicsFlags == null ? new String[0] : physicsFlags.clone();
    }

    void putInto(Intent intent) {
        intent.putExtra("track", trackPath);
        intent.putExtra("car", car);
        intent.putExtra("seconds", seconds);
        intent.putExtra("cores", cores);
        intent.putExtra("strategy", strategy);
        intent.putExtra("fine_tune", fineTune);
        intent.putExtra("transmission", transmission);
        intent.putExtra("physics", physicsFlags);
    }

    static GenerationRequest from(Intent intent) {
        return new GenerationRequest(intent.getStringExtra("track"), intent.getStringExtra("car"),
            intent.getIntExtra("seconds", DEFAULT_SECONDS),
            intent.getIntExtra("cores", DEFAULT_CORES),
            intent.getStringExtra("strategy"),
            intent.getBooleanExtra("fine_tune", false),
            intent.getStringExtra("transmission"), intent.getStringArrayExtra("physics"));
    }

    File validate(File game) throws IOException {
        if (!car.matches("[A-Z0-9]{4}")) {
            throw new IOException("Use a four-character car code, such as COUN or DIA3.");
        }
        if (seconds < 1 || seconds > 86400) {
            throw new IOException("Choose a search budget between 1 second and 24 hours.");
        }
        if (cores < 0 || cores > MAX_CORES) {
            throw new IOException("Choose between 1 and " + MAX_CORES
                + " cores, or 0 for all available cores.");
        }
        if (!strategy.equals("legacy") && !strategy.equals("state")) {
            throw new IOException("Unknown search method.");
        }
        if (!transmission.equals("source") && !transmission.equals("manual")
                && !transmission.equals("auto")) {
            throw new IOException("Unknown transmission setting.");
        }
        Set<String> options = new HashSet<>();
        for (String flag : physicsFlags) {
            if (flag == null || !(flag.equals("--owoot") || flag.equals("/amigagrass")
                    || flag.matches("--(pg|lc|lcb|dws):(on|off)"))) {
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
        if (!name.endsWith(".trk") && !name.endsWith(".rpl")) {
            throw new IOException("Choose a .TRK track or .RPL replay.");
        }
        if (fineTune && !name.endsWith(".rpl")) {
            throw new IOException("Fine-tuning requires a completed .RPL replay.");
        }
        return track;
    }

    List<String> arguments(File executable, File game, File track, File output) {
        List<String> command = new ArrayList<>();
        command.add(executable.getAbsolutePath());
        command.add("--data-dir");
        command.add(game.getAbsolutePath());
        command.add(track.getAbsolutePath());
        command.add(car);
        command.add("--seconds");
        command.add(Integer.toString(seconds));
        command.add("--cores");
        command.add(Integer.toString(cores));
        command.add("--strategy");
        command.add(strategy);
        if (fineTune) {
            command.add("--fine-tune");
        }
        command.add("--output");
        command.add(output.getAbsolutePath());
        if (!transmission.equals("source")) {
            command.add("--transmission");
            command.add(transmission);
        }
        for (String flag : physicsFlags) {
            command.add(flag);
        }
        return command;
    }
}
