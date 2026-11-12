package org.restunts.genghost;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.Enumeration;
import java.util.Locale;
import java.util.Set;
import java.util.TreeSet;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.zip.CRC32;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;
import org.restunts.android.GameDataImport;

/** Bundled car extraction and the car roots shared with the native resource loader. */
final class CarResources {
    static final String DIRECTORY = "cars";
    static final String ASSETS_ENVIRONMENT = "RESTUNTS_ASSETS_DIR";
    private static final Pattern RESOURCE_NAME = Pattern.compile(
        "[A-Z0-9_]{1,8}\\.(RES|PRE|3SH|P3S|H3D|VSH|PVS)", Pattern.CASE_INSENSITIVE);
    private static final String CAR_CODE_PATTERN = "[A-Z0-9_]{4}";
    private static final Pattern CAR_CODE = Pattern.compile(CAR_CODE_PATTERN, Pattern.CASE_INSENSITIVE);
    private static final Pattern CAR_NAME = Pattern.compile("CAR(" + CAR_CODE_PATTERN + ")\\.(RES|PRE)");
    private static final Pattern ARCHIVE_NAME = Pattern.compile("(" + CAR_CODE_PATTERN + ")\\.ZIP",
        Pattern.CASE_INSENSITIVE);
    private static final int CAR_CODE_GROUP = 1;
    private static final int MAX_ARCHIVE_RESOURCE_BYTES = 16 * 1024 * 1024;

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
            if (!RESOURCE_NAME.matcher(name).matches() && !ARCHIVE_NAME.matcher(name).matches()) {
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
            if (!file.isFile()) {
                continue;
            }
            Matcher name = CAR_NAME.matcher(file.getName().toUpperCase(Locale.ROOT));
            Matcher archive = ARCHIVE_NAME.matcher(file.getName());
            if (name.matches()) {
                cars.add(name.group(CAR_CODE_GROUP));
            } else if (archive.matches()) {
                String code = archive.group(CAR_CODE_GROUP).toUpperCase(Locale.ROOT);
                if (archiveHasCar(file, code)) {
                    cars.add(code);
                }
            }
        }
    }

    private static boolean archiveHasCar(File file, String code) {
        try (ZipFile archive = new ZipFile(file)) {
            String[] names = {"CAR" + code + ".RES", "CAR" + code + ".PRE"};
            ZipEntry[] physics = new ZipEntry[names.length];
            boolean[] duplicates = new boolean[names.length];
            Enumeration<? extends ZipEntry> entries = archive.entries();
            while (entries.hasMoreElements()) {
                ZipEntry entry = entries.nextElement();
                String name = entry.getName().toUpperCase(Locale.ROOT);
                for (int index = 0; index < names.length; index++) {
                    if (name.equals(names[index])) {
                        if (physics[index] != null) {
                            duplicates[index] = true;
                        } else {
                            physics[index] = entry;
                        }
                    }
                }
            }
            // Resolve each representation only after checking for case-insensitive duplicates.
            for (int index = 0; index < physics.length; index++) {
                if (duplicates[index] || physics[index] == null) {
                    continue;
                }
                try {
                    if (archiveEntryValid(archive, physics[index])) {
                        return true;
                    }
                } catch (IOException invalidMember) {
                    // A broken RES can still have a valid PRE alternative, matching native lookup.
                }
            }
        } catch (IOException invalidArchive) {
            // An unrelated or broken ZIP must not prevent selecting the other installed cars.
        }
        return false;
    }

    private static boolean archiveEntryValid(ZipFile archive, ZipEntry entry) throws IOException {
        if (entry.isDirectory() || entry.getSize() < 0 || entry.getSize() > MAX_ARCHIVE_RESOURCE_BYTES ||
            entry.getCompressedSize() < 0 || entry.getCompressedSize() > MAX_ARCHIVE_RESOURCE_BYTES ||
            (entry.getMethod() != ZipEntry.STORED && entry.getMethod() != ZipEntry.DEFLATED)) {
            return false;
        }
        // Validate the selected member without unpacking files into the game directory.
        CRC32 crc = new CRC32();
        long size = 0;
        byte[] buffer = new byte[GameDataImport.BUFFER_SIZE];
        try (InputStream input = archive.getInputStream(entry)) {
            int count;
            while ((count = input.read(buffer)) != -1) {
                size += count;
                if (size > MAX_ARCHIVE_RESOURCE_BYTES || size > entry.getSize()) {
                    return false;
                }
                crc.update(buffer, 0, count);
            }
        }
        return size == entry.getSize() && crc.getValue() == entry.getCrc();
    }
}
