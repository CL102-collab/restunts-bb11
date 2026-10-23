package org.restunts.genghost;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.Locale;
import java.util.Set;
import java.util.TreeSet;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import org.restunts.android.GameDataImport;

/** Bundled car extraction and the car roots shared with the native resource loader. */
final class CarResources {
    static final String DIRECTORY = "cars";
    static final String ASSETS_ENVIRONMENT = "RESTUNTS_ASSETS_DIR";
    private static final Pattern RESOURCE_NAME = Pattern.compile(
        "[A-Z0-9_]{1,8}\\.(RES|PRE|3SH|P3S|VSH|PVS)", Pattern.CASE_INSENSITIVE);
    private static final String CAR_CODE_PATTERN = "[A-Z0-9_]{4}";
    private static final Pattern CAR_CODE = Pattern.compile(CAR_CODE_PATTERN, Pattern.CASE_INSENSITIVE);
    private static final Pattern CAR_NAME = Pattern.compile("CAR(" + CAR_CODE_PATTERN + ")\\.(RES|PRE)");
    private static final int CAR_CODE_GROUP = 1;

    interface Source {
        InputStream open(String name) throws IOException;
    }

    static void prepare(File assets, String[] names, Source source) throws IOException {
        if (names == null || names.length == 0) {
            throw new IOException("The bundled car resources are unavailable.");
        }
        File directory = new File(assets, DIRECTORY);
        GameDataImport.makeDirectory(directory);
        for (String name : names) {
            if (!RESOURCE_NAME.matcher(name).matches()) {
                continue;
            }
            File destination = new File(directory, name.toLowerCase(Locale.ROOT));
            try (InputStream input = source.open(name);
                 FileOutputStream output = new FileOutputStream(destination)) {
                byte[] buffer = new byte[GameDataImport.BUFFER_SIZE];
                int count;
                while ((count = input.read(buffer)) != -1) {
                    output.write(buffer, 0, count);
                }
            }
        }
    }

    static boolean validCode(String code) {
        return code != null && CAR_CODE.matcher(code).matches();
    }

    static Set<String> available(File game, File assets) throws IOException {
        Set<String> cars = new TreeSet<>();
        collect(game, cars, true);
        collect(new File(game, DIRECTORY), cars, false);
        collect(new File(assets, DIRECTORY), cars, false);
        return cars;
    }

    private static void collect(File directory, Set<String> cars, boolean required) throws IOException {
        if (!required && !directory.exists()) {
            return;
        }
        File[] files = directory.listFiles();
        if (files == null) {
            throw new IOException("Cannot read car files.");
        }
        for (File file : files) {
            Matcher name = CAR_NAME.matcher(file.getName().toUpperCase(Locale.ROOT));
            if (file.isFile() && name.matches()) {
                cars.add(name.group(CAR_CODE_GROUP));
            }
        }
    }
}
