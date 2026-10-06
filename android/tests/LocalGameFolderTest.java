package org.restunts.android;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.RandomAccessFile;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.Collections;
import java.util.Comparator;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.Map;
import java.util.Set;
import java.util.stream.Stream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/** Host filesystem and synchronization coverage for the permission-free TV folder. */
public final class LocalGameFolderTest {
    private static final byte[] ORIGINAL = {1, 2, 3};
    private static final byte[] UPDATED = {4, 5, 6, 7};
    private static final byte[] LATEST = {8, 9, 10, 11, 12};
    private static final byte[] EXTERNAL = {13, 14};
    private static final String[] REQUIRED_FILES = {"MAIN.RES", "FONTDEF.FNT", "FONTN.FNT"};
    private static final String RESERVED_TEMP = ".restunts-save-interrupted.tmp";
    private static final String RESERVED_BACKUP = ".restunts-save-interrupted.bak";
    private static final String TEMP_PREFIX = ".restunts-save-";
    private static final String TEMP_SUFFIX = ".tmp";
    private static final short ONE_RESOURCE = 1;
    private static final int FIRST_RESOURCE_OFFSET = 0;
    private static final int NUL_BYTES = 1;
    private static final int TOTAL_TEST_FILES = 2;
    private static final long EXCESS_BYTES = 1L;

    private interface Operation {
        void run() throws IOException;
    }

    private static final class Fixture {
        final File root;
        final File shared;
        final File game;
        final File state;

        Fixture(File parent, String name) throws IOException {
            root = new File(parent, name);
            shared = new File(root, "shared");
            game = new File(root, "private/game");
            state = new File(root, "private/sync.properties");
            for (String required : REQUIRED_FILES) {
                put(new File(shared, required), ORIGINAL);
            }
            put(new File(shared, "MISC.RES"), version(GameDataVersion.EXPECTED_VERSION));
        }

        GameDataSync engine() throws IOException {
            return new GameDataSync(game, state, new LocalGameFolder(shared));
        }

        GameDataSync lazyEngine() {
            GameDataSync.Store lazy = new GameDataSync.Store() {
                private LocalGameFolder opened;

                private LocalGameFolder folder() throws IOException {
                    if (opened == null) {
                        opened = new LocalGameFolder(shared);
                    }
                    return opened;
                }

                @Override
                public Set<String> listFiles() throws IOException {
                    return folder().listFiles();
                }

                @Override
                public Set<String> listDirectories() throws IOException {
                    return folder().listDirectories();
                }

                @Override
                public InputStream read(String name) throws IOException {
                    return folder().read(name);
                }

                @Override
                public void write(String name, InputStream input) throws IOException {
                    folder().write(name, input);
                }

                @Override
                public void makeDirectory(String name) throws IOException {
                    folder().makeDirectory(name);
                }
            };
            return new GameDataSync(game, state, lazy);
        }
    }

    private static void require(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    private static void rejects(Operation operation, String message) throws IOException {
        try {
            operation.run();
            throw new AssertionError(message);
        } catch (IOException expected) {
            require(expected.getMessage() != null, "Failure needs an explanation.");
        }
    }

    private static void put(File file, byte[] bytes) throws IOException {
        Files.createDirectories(file.getParentFile().toPath());
        Files.write(file.toPath(), bytes);
    }

    private static void contents(File file, byte[] expected) throws IOException {
        require(Arrays.equals(Files.readAllBytes(file.toPath()), expected),
            "Unexpected contents: " + file.getName());
    }

    private static byte[] version(String value) {
        byte[] text = value.getBytes(StandardCharsets.US_ASCII);
        int start = GameDataVersion.RESOURCE_DIRECTORY_OFFSET
            + GameDataVersion.RESOURCE_IDENTIFIER_SIZE + GameDataVersion.RESOURCE_OFFSET_SIZE;
        byte[] result = new byte[start + text.length + NUL_BYTES];
        ByteBuffer bytes = ByteBuffer.wrap(result).order(ByteOrder.LITTLE_ENDIAN);
        bytes.putInt(GameDataVersion.RESOURCE_SIZE_OFFSET, result.length);
        bytes.putShort(GameDataVersion.RESOURCE_COUNT_OFFSET, ONE_RESOURCE);
        bytes.position(GameDataVersion.RESOURCE_DIRECTORY_OFFSET);
        bytes.put("gver".getBytes(StandardCharsets.US_ASCII));
        bytes.putInt(FIRST_RESOURCE_OFFSET);
        bytes.put(text);
        return result;
    }

    private static void aliasesAndDirectories(File root) throws IOException {
        File folder = new File(root, "case-preservation");
        put(new File(folder, "Mixed.TRK"), ORIGINAL);
        Files.createDirectories(new File(folder, "Tracks/Empty/Nested").toPath());
        put(new File(folder, RESERVED_TEMP), UPDATED);
        put(new File(folder, RESERVED_BACKUP), ORIGINAL);
        LocalGameFolder store = new LocalGameFolder(folder);
        require(store.listFiles().equals(Collections.singleton("mixed.trk")),
            "Physical case or transaction files leaked into the game index.");
        require(store.listDirectories().contains("tracks/empty/nested"), "Lost empty folders.");
        try (InputStream input = store.read("MIXED.TRK")) {
            byte[] bytes = new byte[ORIGINAL.length];
            require(input.read(bytes) == bytes.length && Arrays.equals(bytes, ORIGINAL),
                "Case-insensitive reads failed.");
        }
        store.write("mixed.trk", new ByteArrayInputStream(UPDATED));
        contents(new File(folder, "Mixed.TRK"), UPDATED);
        require(!new File(folder, "mixed.trk").exists(), "Changed the physical filename's case.");
        store.makeDirectory("TRACKS/New/Nested");
        store.write("tracks/new/nested/Replay.RPL", new ByteArrayInputStream(ORIGINAL));
        contents(new File(folder, "Tracks/new/nested/replay.rpl"), ORIGINAL);
        Set<String> snapshot = store.listFiles();
        snapshot.clear();
        require(store.listFiles().contains("mixed.trk"), "A caller changed the internal index.");
        for (String path : new String[] {RESERVED_TEMP, RESERVED_BACKUP,
                "tracks/" + RESERVED_TEMP, RESERVED_TEMP + "/nested"}) {
            rejects(() -> store.write(path, new ByteArrayInputStream(ORIGINAL)),
                "Accepted a reserved write path.");
            rejects(() -> store.makeDirectory(path), "Accepted a reserved directory path.");
        }
        rejects(() -> store.read(RESERVED_TEMP), "Exposed a transaction artifact.");
    }

    private static void unsafePathsAndLinks(File root) throws IOException {
        File folder = new File(root, "safe-root");
        LocalGameFolder store = new LocalGameFolder(folder);
        for (String path : new String[] {null, "", "../escape", "/absolute", "a/../escape",
                "a\\..\\escape", "a:b", "a\nb", "a//b"}) {
            rejects(() -> store.write(path, new ByteArrayInputStream(ORIGINAL)), "Accepted unsafe path.");
            rejects(() -> store.makeDirectory(path), "Accepted unsafe directory.");
        }
        File duplicate = new File(root, "duplicate-files");
        put(new File(duplicate, "Alias.RES"), ORIGINAL);
        put(new File(duplicate, "ALIAS.RES"), UPDATED);
        rejects(() -> new LocalGameFolder(duplicate), "Accepted duplicate case aliases.");
        File occupied = new File(root, "duplicate-directories");
        Files.createDirectories(new File(occupied, "Tracks").toPath());
        put(new File(occupied, "TRACKS"), ORIGINAL);
        rejects(() -> new LocalGameFolder(occupied), "Accepted file/directory case aliases.");

        File outside = new File(root, "outside");
        put(new File(outside, "keep.res"), EXTERNAL);
        Path rootLink = new File(root, "linked-root").toPath();
        Files.createSymbolicLink(rootLink, outside.toPath());
        rejects(() -> new LocalGameFolder(rootLink.toFile()), "Accepted a linked game root.");
        for (String name : new String[] {"outside-directory", "outside-file", "cycle", "inside-file"}) {
            File linked = new File(root, name);
            Files.createDirectories(linked.toPath());
            Path target = name.equals("cycle") ? linked.toPath()
                : name.equals("outside-directory") ? outside.toPath() : new File(outside, "keep.res").toPath();
            if (name.equals("inside-file")) {
                put(new File(linked, "original.res"), ORIGINAL);
                target = new File(linked, "original.res").toPath();
            }
            Files.createSymbolicLink(new File(linked, "link").toPath(), target);
            rejects(() -> new LocalGameFolder(linked), "Accepted a linked child or directory cycle.");
        }
        File replaced = new File(root, "replaced-link");
        File target = new File(replaced, "saved.rpl");
        put(target, ORIGINAL);
        LocalGameFolder indexed = new LocalGameFolder(replaced);
        Files.delete(target.toPath());
        Files.createSymbolicLink(target.toPath(), new File(outside, "keep.res").toPath());
        rejects(() -> indexed.write("saved.rpl", new ByteArrayInputStream(UPDATED)),
            "Followed a symlink introduced after indexing.");
        rejects(() -> indexed.read("saved.rpl"), "Read outside through a replaced symlink.");
        contents(new File(outside, "keep.res"), EXTERNAL);
    }

    private static InputStream failingStream() {
        return new InputStream() {
            private boolean supplied;

            @Override
            public int read() throws IOException {
                throw new IOException("Injected input failure.");
            }

            @Override
            public int read(byte[] buffer, int offset, int length) throws IOException {
                if (supplied) {
                    throw new IOException("Injected input failure after partial copy.");
                }
                supplied = true;
                int count = Math.min(length, UPDATED.length);
                System.arraycopy(UPDATED, 0, buffer, offset, count);
                return count;
            }
        };
    }

    private static InputStream changingStream(File target) {
        return new InputStream() {
            private boolean supplied;

            @Override
            public int read() throws IOException {
                throw new IOException("Use the buffered test stream.");
            }

            @Override
            public int read(byte[] buffer, int offset, int length) throws IOException {
                if (supplied) {
                    return -1;
                }
                supplied = true;
                put(target, EXTERNAL);
                int count = Math.min(length, UPDATED.length);
                System.arraycopy(UPDATED, 0, buffer, offset, count);
                return count;
            }
        };
    }

    private static InputStream linkedTemporaryStream(File folder, File outside) {
        return new InputStream() {
            private boolean supplied;

            @Override
            public int read() throws IOException {
                throw new IOException("Use the buffered test stream.");
            }

            @Override
            public int read(byte[] buffer, int offset, int length) throws IOException {
                if (supplied) {
                    return -1;
                }
                supplied = true;
                File[] files = folder.listFiles();
                require(files != null, "Cannot find the staged replacement.");
                File temporary = null;
                for (File file : files) {
                    if (file.getName().startsWith(TEMP_PREFIX) && file.getName().endsWith(TEMP_SUFFIX)) {
                        temporary = file;
                    }
                }
                require(temporary != null, "Replacement was not staged before reading input.");
                Files.delete(temporary.toPath());
                Files.createSymbolicLink(temporary.toPath(), outside.toPath());
                int count = Math.min(length, UPDATED.length);
                System.arraycopy(UPDATED, 0, buffer, offset, count);
                return count;
            }
        };
    }

    private static void safeReplacement(File root) throws IOException {
        File folder = new File(root, "replacement");
        File saved = new File(folder, "Saved.RPL");
        put(saved, ORIGINAL);
        LocalGameFolder store = new LocalGameFolder(folder);
        rejects(() -> store.write("saved.rpl", failingStream()), "Committed incomplete output.");
        contents(saved, ORIGINAL);
        require(store.listFiles().equals(Collections.singleton("saved.rpl")), "Exposed failed temp copy.");
        File[] files = folder.listFiles();
        require(files != null && files.length == store.listFiles().size(),
            "Did not clean the failed temporary copy.");
        rejects(() -> store.write("saved.rpl", null), "Accepted missing input.");
        contents(saved, ORIGINAL);
        store.write("saved.rpl", new ByteArrayInputStream(UPDATED));
        contents(saved, UPDATED);
        rejects(() -> store.write("saved.rpl", changingStream(saved)), "Overwrote an external edit.");
        contents(saved, EXTERNAL);
        LocalGameFolder fresh = new LocalGameFolder(folder);
        File appeared = new File(folder, "New.RPL");
        rejects(() -> fresh.write("new.rpl", changingStream(appeared)), "Overwrote a newly added file.");
        contents(appeared, EXTERNAL);
        File outside = new File(root, "temporary-link-outside");
        put(outside, ORIGINAL);
        LocalGameFolder after = new LocalGameFolder(folder);
        rejects(() -> after.write("saved.rpl", linkedTemporaryStream(folder, outside)),
            "Committed a linked temporary file.");
        contents(saved, EXTERNAL);
        contents(outside, ORIGINAL);
        rejects(() -> fresh.makeDirectory("saved.rpl/nested"), "Used a file as a directory.");
        fresh.makeDirectory("empty");
        rejects(() -> fresh.write("empty", new ByteArrayInputStream(ORIGINAL)), "Replaced a directory.");
    }

    private static void sparse(File path, long size) throws IOException {
        Files.createDirectories(path.getParentFile().toPath());
        try (RandomAccessFile file = new RandomAccessFile(path, "rw")) {
            file.setLength(size);
        }
    }

    private static void limits(File root) throws IOException {
        File excessive = new File(root, "oversize-file");
        sparse(new File(excessive, "large.webm"), GameDataSync.MAX_FILE_BYTES + EXCESS_BYTES);
        rejects(() -> new LocalGameFolder(excessive), "Accepted an oversized file.");
        File total = new File(root, "oversize-total");
        long part = GameDataSync.MAX_TOTAL_BYTES / TOTAL_TEST_FILES + EXCESS_BYTES;
        for (int index = 0; index < TOTAL_TEST_FILES; index++) {
            sparse(new File(total, "part" + index + ".webm"), part);
        }
        rejects(() -> new LocalGameFolder(total), "Accepted excessive aggregate size.");
        File full = new File(root, "full-bytes");
        File big = new File(full, "large.webm");
        sparse(big, GameDataSync.MAX_TOTAL_BYTES);
        LocalGameFolder quota = new LocalGameFolder(full);
        rejects(() -> quota.write("extra.trk", new ByteArrayInputStream(ORIGINAL)), "Exceeded write quota.");
        require(big.length() == GameDataSync.MAX_TOTAL_BYTES, "A failed write changed existing data.");
        require(!new File(full, "extra.trk").exists(), "Committed an oversized addition.");
        quota.write("large.webm", new ByteArrayInputStream(ORIGINAL));
        contents(big, ORIGINAL);

        File count = new File(root, "full-count");
        Files.createDirectories(count.toPath());
        for (int index = 0; index < GameDataSync.MAX_FILES; index++) {
            Files.createFile(new File(count, "file" + index).toPath());
        }
        LocalGameFolder maximum = new LocalGameFolder(count);
        rejects(() -> maximum.write("new.trk", new ByteArrayInputStream(ORIGINAL)), "Exceeded file count.");
        rejects(() -> maximum.makeDirectory("new"), "Exceeded count by adding a directory.");
        Files.createDirectory(new File(count, "extra-directory").toPath());
        rejects(() -> new LocalGameFolder(count), "Did not count files and directories together.");

        File nested = new File(root, "depth");
        File deepest = nested;
        StringBuilder relative = new StringBuilder();
        for (int index = 0; index < GameDataSync.MAX_DEPTH; index++) {
            deepest = new File(deepest, "nested");
            if (relative.length() != 0) {
                relative.append('/');
            }
            relative.append("nested");
        }
        Files.createDirectories(deepest.toPath());
        LocalGameFolder depth = new LocalGameFolder(nested);
        require(depth.listDirectories().contains(relative.toString()), "Rejected valid maximum depth.");
        rejects(() -> depth.makeDirectory(relative + "/extra"), "Created a folder beyond depth limit.");
        put(new File(deepest, "extra.res"), ORIGINAL);
        rejects(() -> new LocalGameFolder(nested), "Indexed a file beyond depth limit.");
    }

    private static void refreshAndValidation(File root) throws IOException {
        Fixture fixture = new Fixture(root, "refresh");
        put(new File(fixture.shared, "Custom.RES"), ORIGINAL);
        Files.createDirectories(new File(fixture.shared, "Tracks/Empty").toPath());
        fixture.engine().refresh();
        contents(new File(fixture.game, "main.res"), ORIGINAL);
        require(new File(fixture.game, "tracks/empty").isDirectory(), "Refresh discarded empty directory.");
        put(new File(fixture.shared, "Custom.RES"), EXTERNAL);
        Files.delete(new File(fixture.shared, "FONTN.FNT").toPath());
        rejects(() -> fixture.engine().refresh(), "Accepted incomplete external game files.");
        contents(new File(fixture.game, "custom.res"), ORIGINAL);
        put(new File(fixture.shared, "FONTN.FNT"), ORIGINAL);
        put(new File(fixture.shared, "MISC.RES"), version("Unsupported version"));
        rejects(() -> fixture.engine().refresh(), "Accepted the wrong Stunts version.");
        contents(new File(fixture.game, "custom.res"), ORIGINAL);
        put(new File(fixture.shared, "MISC.RES"), version(GameDataVersion.EXPECTED_VERSION));
        fixture.engine().refresh();
        contents(new File(fixture.game, "custom.res"), EXTERNAL);
        Files.delete(new File(fixture.shared, "Custom.RES").toPath());
        Files.delete(new File(fixture.shared, "Tracks/Empty").toPath());
        Files.createDirectories(new File(fixture.shared, "Cars/NewEmpty").toPath());
        fixture.engine().refresh();
        require(!new File(fixture.game, "custom.res").exists(), "Kept deleted external data.");
        require(!new File(fixture.game, "tracks/empty").exists(), "Kept deleted empty subfolder.");
        require(new File(fixture.game, "cars/newempty").isDirectory(), "Lost new external empty subfolder.");
        put(new File(fixture.game, "tracks/saved.rpl"), UPDATED);
        fixture.engine().publish("TRACKS/SAVED.RPL");
        contents(new File(fixture.shared, "Tracks/saved.rpl"), UPDATED);
    }

    private static File block(Fixture fixture) throws IOException {
        File held = new File(fixture.root, "held-game-folder");
        require(fixture.shared.renameTo(held), "Cannot temporarily remove test folder.");
        put(fixture.shared, ORIGINAL);
        return held;
    }

    private static void unblock(Fixture fixture, File held) throws IOException {
        Files.delete(fixture.shared.toPath());
        require(held.renameTo(fixture.shared), "Cannot restore test folder.");
    }

    private static void pendingWrites(File root) throws IOException {
        Fixture fixture = new Fixture(root, "pending-retry");
        put(new File(fixture.shared, "Saved.RPL"), ORIGINAL);
        fixture.engine().refresh();
        File held = block(fixture);
        put(new File(fixture.game, "saved.rpl"), UPDATED);
        rejects(() -> fixture.lazyEngine().publish("saved.rpl"), "Ignored unavailable local folder.");
        File pending = new File(fixture.state.getPath() + ".pending/saved.rpl");
        contents(pending, UPDATED);
        put(new File(fixture.game, "saved.rpl"), LATEST);
        rejects(() -> fixture.lazyEngine().publish("saved.rpl"), "Ignored repeated publication failure.");
        contents(pending, LATEST);
        unblock(fixture, held);
        fixture.engine().refresh();
        contents(new File(fixture.shared, "Saved.RPL"), LATEST);
        contents(new File(fixture.game, "saved.rpl"), LATEST);
        require(!pending.exists(), "Did not clear the completed pending write.");

        Fixture conflict = new Fixture(root, "pending-conflict");
        put(new File(conflict.shared, "Saved.RPL"), ORIGINAL);
        conflict.engine().refresh();
        File preserved = block(conflict);
        put(new File(conflict.game, "saved.rpl"), UPDATED);
        rejects(() -> conflict.lazyEngine().publish("saved.rpl"), "Ignored unavailable folder.");
        unblock(conflict, preserved);
        put(new File(conflict.shared, "Saved.RPL"), EXTERNAL);
        rejects(() -> conflict.engine().refresh(), "Overwrote an external edit while retrying.");
        contents(new File(conflict.shared, "Saved.RPL"), EXTERNAL);
        contents(new File(conflict.game, "saved.rpl"), UPDATED);
        contents(new File(conflict.state.getPath() + ".pending/saved.rpl"), UPDATED);
        Files.move(new File(conflict.shared, "Saved.RPL").toPath(),
            new File(conflict.shared, "Kept.RPL").toPath());
        conflict.engine().refresh();
        contents(new File(conflict.shared, "saved.rpl"), UPDATED);
        contents(new File(conflict.shared, "Kept.RPL"), EXTERNAL);
    }

    private static File zip(Fixture fixture, String name, Map<String, byte[]> additional,
            Set<String> directories) throws IOException {
        File staging = new File(fixture.root, name);
        GameDataImport.prepare(staging);
        Map<String, byte[]> entries = new LinkedHashMap<>();
        for (String required : REQUIRED_FILES) {
            entries.put(required, ORIGINAL);
        }
        entries.put("MISC.RES", version(GameDataVersion.EXPECTED_VERSION));
        entries.putAll(additional);
        ByteArrayOutputStream data = new ByteArrayOutputStream();
        try (ZipOutputStream archive = new ZipOutputStream(data)) {
            for (String directory : directories) {
                archive.putNextEntry(new ZipEntry("OriginalGame/" + directory + "/"));
                archive.closeEntry();
            }
            for (Map.Entry<String, byte[]> entry : entries.entrySet()) {
                archive.putNextEntry(new ZipEntry("OriginalGame/" + entry.getKey()));
                archive.write(entry.getValue());
                archive.closeEntry();
            }
        }
        GameDataImport importer = new GameDataImport(staging);
        importer.readZip(new ByteArrayInputStream(data.toByteArray()));
        return importer.gameDirectory();
    }

    private static void zipImports(File root) throws IOException {
        Fixture fixture = new Fixture(root, "zip-import");
        put(new File(fixture.shared, "Keep.CFG"), EXTERNAL);
        Map<String, byte[]> additions = new LinkedHashMap<>();
        additions.put("Cars/CUSTOM.RES", UPDATED);
        Set<String> directories = new LinkedHashSet<>();
        directories.add("Tracks/Empty/Nested");
        File source = zip(fixture, "valid-zip", additions, directories);
        fixture.engine().installZip(source);
        fixture.engine().refresh();
        contents(new File(fixture.shared, "MAIN.RES"), ORIGINAL);
        contents(new File(fixture.shared, "Keep.CFG"), EXTERNAL);
        contents(new File(fixture.shared, "cars/custom.res"), UPDATED);
        require(new File(fixture.game, "tracks/empty/nested").isDirectory(), "ZIP lost empty folders.");
        fixture.engine().installZip(source);
        contents(new File(fixture.shared, "cars/custom.res"), UPDATED);

        put(new File(fixture.shared, "cars/custom.res"), EXTERNAL);
        additions.put("Before.TRK", ORIGINAL);
        File conflict = zip(fixture, "different-zip", additions, directories);
        rejects(() -> fixture.engine().installZip(conflict), "Overwrote conflicting ZIP destination.");
        require(!new File(fixture.shared, "before.trk").exists(), "Wrote files before ZIP preflight finished.");
        contents(new File(fixture.shared, "cars/custom.res"), EXTERNAL);
        put(new File(source, "misc.res"), version("Unsupported version"));
        rejects(() -> fixture.engine().installZip(source), "Installed wrong-version ZIP root.");
        contents(new File(fixture.shared, "MISC.RES"), version(GameDataVersion.EXPECTED_VERSION));

        Fixture collision = new Fixture(root, "zip-collision");
        put(new File(collision.shared, "Occupied"), ORIGINAL);
        Set<String> occupied = Collections.singleton("Occupied/Nested");
        File blocked = zip(collision, "collision-zip", additions, occupied);
        rejects(() -> collision.engine().installZip(blocked), "Used an existing file as a ZIP directory.");
        contents(new File(collision.shared, "Occupied"), ORIGINAL);
        require(!new File(collision.shared, "before.trk").exists(), "Partially installed conflicting ZIP.");
    }

    private static void remove(File root) throws IOException {
        try (Stream<Path> paths = Files.walk(root.toPath())) {
            for (Path path : (Iterable<Path>) paths.sorted(Comparator.reverseOrder())::iterator) {
                Files.delete(path);
            }
        }
    }

    public static void main(String[] arguments) throws IOException {
        File root = Files.createTempDirectory("restunts-local-game-folder-test").toFile();
        try {
            aliasesAndDirectories(root);
            unsafePathsAndLinks(root);
            safeReplacement(root);
            limits(root);
            refreshAndValidation(root);
            pendingWrites(root);
            zipImports(root);
            System.out.println("LocalGameFolderTest passed");
        } finally {
            remove(root);
        }
    }
}
