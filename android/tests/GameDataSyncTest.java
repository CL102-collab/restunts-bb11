package org.restunts.android;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
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

/** Host-only coverage for a public game folder and its replaceable private cache. */
public final class GameDataSyncTest {
    private static final byte[] ORIGINAL = {1, 2, 3};
    private static final byte[] LOCAL_CHANGE = {4, 5, 6};
    private static final byte[] PUBLIC_CHANGE = {7, 8, 9};
    private static final byte[] IMPORTED = {10, 11, 12};
    private static final String[] REQUIRED_FILES = {"main.res", "fontdef.fnt", "fontn.fnt"};
    private static final short ONE_RESOURCE = 1;
    private static final int FIRST_RESOURCE_OFFSET = 0;
    private static final int NUL_BYTES = 1;
    private static final int CUSTOM_VIDEO_BYTES = 16 * 1024 * 1024 + 1;
    private static final long OVERSIZE_BYTES = GameDataSync.MAX_TOTAL_BYTES + 1L;

    private interface Operation {
        void run() throws IOException;
    }

    private static final class MemoryStore implements GameDataSync.Store {
        private final Map<String, byte[]> files = new LinkedHashMap<>();
        private final Set<String> directories = new LinkedHashSet<>();
        private boolean failWrites;
        private boolean failList;
        private String failRead;
        private String oversized;
        private int writes;
        private int directoryWrites;

        void put(String path, byte[] content) {
            files.put(path, content.clone());
            addParents(path);
        }

        void directory(String path) {
            directories.add(path);
            addParents(path);
        }

        private void addParents(String path) {
            int separator = path.lastIndexOf('/');
            while (separator > 0) {
                path = path.substring(0, separator);
                directories.add(path);
                separator = path.lastIndexOf('/');
            }
        }

        private String listedDirectory(String relative) throws IOException {
            String found = null;
            for (String listed : directories) {
                if (GameDataSync.normalizePath(listed).equals(relative)) {
                    if (found != null) {
                        throw new IOException("Ambiguous public directory: " + relative);
                    }
                    found = listed;
                }
            }
            return found;
        }

        void requireContents(String path, byte[] content) {
            require(Arrays.equals(files.get(path), content), "Unexpected public file: " + path);
        }

        private String listedPath(String relative) throws IOException {
            String found = null;
            for (String listed : files.keySet()) {
                if (GameDataSync.normalizePath(listed).equals(relative)) {
                    if (found != null) {
                        throw new IOException("Ambiguous public path: " + relative);
                    }
                    found = listed;
                }
            }
            return found;
        }

        @Override
        public Set<String> listFiles() throws IOException {
            if (failList) {
                throw new IOException("Injected document provider listing failure.");
            }
            Set<String> names = new LinkedHashSet<>(files.keySet());
            if (oversized != null) {
                names.add(oversized);
            }
            return names;
        }

        @Override
        public Set<String> listDirectories() throws IOException {
            if (failList) {
                throw new IOException("Injected document provider listing failure.");
            }
            return new LinkedHashSet<>(directories);
        }

        @Override
        public void makeDirectory(String relative) throws IOException {
            directoryWrites++;
            String name = GameDataSync.normalizePath(relative);
            String prefix = "";
            for (String component : name.split("/")) {
                prefix = prefix.isEmpty() ? component : prefix + "/" + component;
                if (listedPath(prefix) != null) {
                    throw new IOException("A file occupies the public directory: " + prefix);
                }
                if (listedDirectory(prefix) == null) {
                    directories.add(prefix);
                }
            }
        }

        @Override
        public InputStream read(String relative) throws IOException {
            if (relative.equals(failRead)) {
                throw new IOException("Injected document provider read failure.");
            }
            if (relative.equals(oversized)) {
                return new InputStream() {
                    private long remaining = OVERSIZE_BYTES;

                    @Override
                    public int read() {
                        if (remaining == 0L) {
                            return -1;
                        }
                        remaining--;
                        return 0;
                    }

                    @Override
                    public int read(byte[] buffer, int offset, int length) {
                        if (length == 0) {
                            return 0;
                        }
                        if (remaining == 0L) {
                            return -1;
                        }
                        int count = (int) Math.min(remaining, length);
                        Arrays.fill(buffer, offset, offset + count, (byte) 0);
                        remaining -= count;
                        return count;
                    }
                };
            }
            // The SAF adapter indexes physical names by their canonical game path.
            byte[] content = files.get(listedPath(relative));
            if (content == null) {
                throw new IOException("No public file: " + relative);
            }
            return new ByteArrayInputStream(content);
        }

        @Override
        public void write(String relative, InputStream input) throws IOException {
            writes++;
            if (listedDirectory(relative) != null) {
                throw new IOException("A directory occupies the public file: " + relative);
            }
            String parent = relative;
            int parentSeparator = parent.lastIndexOf('/');
            while (parentSeparator > 0) {
                parent = parent.substring(0, parentSeparator);
                if (listedPath(parent) != null) {
                    throw new IOException("A file occupies the public parent: " + parent);
                }
                parentSeparator = parent.lastIndexOf('/');
            }
            ByteArrayOutputStream output = new ByteArrayOutputStream();
            byte[] buffer = new byte[GameDataImport.BUFFER_SIZE];
            int count;
            while ((count = input.read(buffer)) != -1) {
                if (failWrites) {
                    throw new IOException("Injected document provider write failure.");
                }
                output.write(buffer, 0, count);
            }
            if (failWrites) {
                throw new IOException("Injected document provider write failure.");
            }
            String existing = listedPath(relative);
            String destination = existing == null ? relative : existing;
            files.put(destination, output.toByteArray());
            int separator = destination.lastIndexOf('/');
            while (separator > 0) {
                destination = destination.substring(0, separator);
                if (listedDirectory(GameDataSync.normalizePath(destination)) == null) {
                    directories.add(destination);
                }
                separator = destination.lastIndexOf('/');
            }
        }
    }

    private static final class Fixture {
        private final File root;
        private final File game;
        private final File state;
        private final MemoryStore store = new MemoryStore();

        Fixture(File parent, String name) throws IOException {
            root = new File(parent, name);
            game = new File(root, "game");
            state = new File(root, "state");
            Files.createDirectories(game.toPath());
            for (String path : REQUIRED_FILES) {
                store.put(path, ORIGINAL);
            }
            store.put("misc.res", versionResource("gver", GameDataVersion.EXPECTED_VERSION));
        }

        GameDataSync engine() {
            return new GameDataSync(game, state, store);
        }

        void put(String path, byte[] content) throws IOException {
            putFile(new File(game, path), content);
        }

        void requireContents(String path, byte[] content) throws IOException {
            requireFile(new File(game, path), content);
        }
    }

    private static byte[] versionResource(String identifier, String version) {
        byte[] text = version.getBytes(StandardCharsets.US_ASCII);
        int dataOffset = GameDataVersion.RESOURCE_DIRECTORY_OFFSET
            + GameDataVersion.RESOURCE_IDENTIFIER_SIZE + GameDataVersion.RESOURCE_OFFSET_SIZE;
        byte[] resource = new byte[dataOffset + text.length + NUL_BYTES];
        ByteBuffer bytes = ByteBuffer.wrap(resource).order(ByteOrder.LITTLE_ENDIAN);
        bytes.putInt(GameDataVersion.RESOURCE_SIZE_OFFSET, resource.length);
        bytes.putShort(GameDataVersion.RESOURCE_COUNT_OFFSET, ONE_RESOURCE);
        bytes.position(GameDataVersion.RESOURCE_DIRECTORY_OFFSET);
        bytes.put(identifier.getBytes(StandardCharsets.US_ASCII));
        bytes.putInt(FIRST_RESOURCE_OFFSET);
        bytes.put(text);
        return resource;
    }

    private static void require(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    private static void requireFile(File file, byte[] content) throws IOException {
        require(Arrays.equals(Files.readAllBytes(file.toPath()), content),
            "Unexpected private file: " + file.getName());
    }

    private static void putFile(File file, byte[] content) throws IOException {
        Files.createDirectories(file.getParentFile().toPath());
        Files.write(file.toPath(), content);
    }

    private static void rejects(Operation operation, String message) throws IOException {
        try {
            operation.run();
            throw new AssertionError(message);
        } catch (IOException expected) {
            // Failure must be recoverable without destroying the installed game.
        }
    }

    private static void paths() throws IOException {
        require(GameDataSync.normalizePath("Cars\\CUSTOM.RES").equals("cars/custom.res"),
            "Game paths must normalize separators and case.");
        for (String path : new String[] {null, "", "../outside.res", "/absolute.cfg",
                "C:\\escape.ini", "folder\\..\\outside.rpl", "./main.res", "folder//main.res",
                "folder/", "bad\u0000.res", "bad\n.trk", "bad\u007f.hig"}) {
            rejects(() -> GameDataSync.normalizePath(path), "Accepted unsafe game path: " + path);
        }
        StringBuilder deep = new StringBuilder();
        for (int index = 0; index < GameDataSync.MAX_DEPTH; index++) {
            deep.append("directory/");
        }
        deep.append("custom.res");
        rejects(() -> GameDataSync.normalizePath(deep.toString()),
            "Accepted excessive path depth.");
    }

    private static void bundledHorizonAndOverride(File root) throws IOException {
        Fixture fixture = new Fixture(root, "bundled-horizon");
        File bundled = new File(fixture.root, "horizons/Antarctica.png");
        putFile(bundled, ORIGINAL);
        fixture.engine().refresh();
        requireFile(bundled, ORIGINAL);
        require(!new File(fixture.game, "horizons/antarctica.png").exists(),
            "Copied bundled artwork into the synchronized game folder.");

        fixture.store.put("Horizons/Antarctica.png", PUBLIC_CHANGE);
        fixture.engine().refresh();
        fixture.requireContents("horizons/antarctica.png", PUBLIC_CHANGE);
        requireFile(bundled, ORIGINAL);

        fixture.store.files.remove("Horizons/Antarctica.png");
        fixture.engine().refresh();
        require(!new File(fixture.game, "horizons/antarctica.png").exists(),
            "Kept a removed public horizon override instead of allowing bundled fallback.");
        requireFile(bundled, ORIGINAL);
        require(fixture.store.writes == 0, "Published bundled artwork into the selected folder.");
    }

    private static void refreshAllContent(File root) throws IOException {
        Fixture fixture = new Fixture(root, "all-content");
        String[] customFiles = {"cars/custom.res", "cars/custom.pre", "cars/custom.3sh",
            "cars/custom.p3s", "settings.cfg", "controls.ini", "custom.dat", "extensionless",
            "tracks/custom.trk", "replays/custom.rpl", "scores/custom.hig",
            "opponents/animations/custom.webm"};
        for (String path : customFiles) {
            fixture.store.put(path, PUBLIC_CHANGE);
        }
        fixture.store.put("Menus/Custom.PNG", PUBLIC_CHANGE);
        fixture.put("main.res", LOCAL_CHANGE);
        fixture.put("private-only.trk", LOCAL_CHANGE);
        fixture.put("obsolete/car.res", LOCAL_CHANGE);
        fixture.engine().refresh();
        for (String path : customFiles) {
            fixture.requireContents(path, PUBLIC_CHANGE);
        }
        fixture.requireContents("menus/custom.png", PUBLIC_CHANGE);
        fixture.requireContents("main.res", ORIGINAL);
        require(!new File(fixture.game, "private-only.trk").exists(),
            "Retained a private-only file during folder refresh.");
        require(!new File(fixture.game, "obsolete").exists(), "Kept obsolete private resources.");
        require(fixture.store.writes == 0, "Refresh changed the authoritative game folder.");

        fixture.store.put("cars/custom.res", IMPORTED);
        fixture.store.put("new/nested/car.3sh", IMPORTED);
        fixture.store.files.remove("tracks/custom.trk");
        fixture.store.files.remove("opponents/animations/custom.webm");
        fixture.store.directories.remove("opponents/animations");
        fixture.store.directories.remove("opponents");
        fixture.put("cars/custom.res", LOCAL_CHANGE);
        fixture.engine().refresh();
        fixture.requireContents("cars/custom.res", IMPORTED);
        fixture.requireContents("new/nested/car.3sh", IMPORTED);
        require(!new File(fixture.game, "tracks/custom.trk").exists(),
            "Resurrected a save deleted from the public folder.");
        require(!new File(fixture.game, "opponents").exists(), "Kept a deleted public subtree.");
        require(!fixture.store.files.containsKey("tracks/custom.trk"),
            "Republished a save deleted from the public folder.");
        require(fixture.store.writes == 0, "Refresh published stale cache contents.");
    }

    private static void failedRefresh(File root) throws IOException {
        Fixture fixture = new Fixture(root, "failed-refresh");
        fixture.store.put("last/custom.res", ORIGINAL);
        fixture.engine().refresh();
        byte[] state = Files.readAllBytes(fixture.state.toPath());
        fixture.store.put("main.res", PUBLIC_CHANGE);
        fixture.store.failRead = "last/custom.res";
        rejects(() -> fixture.engine().refresh(), "Installed a partially copied game folder.");
        fixture.requireContents("main.res", ORIGINAL);
        fixture.requireContents("last/custom.res", ORIGINAL);
        require(Arrays.equals(state, Files.readAllBytes(fixture.state.toPath())),
            "Changed the baseline after an incomplete refresh.");
        require(fixture.store.writes == 0, "Failed refresh wrote into the public folder.");
        fixture.store.failRead = null;
        fixture.engine().refresh();
        fixture.requireContents("main.res", PUBLIC_CHANGE);
    }

    private static void packedRefresh(File root) throws IOException {
        Fixture fixture = new Fixture(root, "packed-data");
        byte[] packed = GameDataVersionTest.literalRle(GameDataVersionTest.versionResource());
        fixture.store.files.remove("main.res");
        fixture.store.files.remove("misc.res");
        fixture.store.put("MAIN.PRE", ORIGINAL);
        fixture.store.put("MISC.PRE", packed);
        fixture.engine().refresh();
        fixture.requireContents("main.pre", ORIGINAL);
        fixture.requireContents("misc.pre", packed);
        require(!new File(fixture.game, "main.res").exists(),
            "Required an unpacked MAIN override.");
        require(fixture.store.writes == 0, "Packed refresh modified the public folder.");

        fixture.store.put("MISC.RES", versionResource("gver", "Version 1.0 (Feb 12 1991)"));
        rejects(() -> fixture.engine().refresh(),
            "Ignored an incompatible unpacked MISC override.");
        fixture.requireContents("misc.pre", packed);
        require(!new File(fixture.game, "misc.res").exists(),
            "Installed an invalid MISC override.");
    }

    private static void publishEveryFormatAndRetry(File root) throws IOException {
        Fixture fixture = new Fixture(root, "publish");
        fixture.engine().refresh();
        String[] outputs = {"settings.cfg", "controls.ini", "session.dat", "extensionless",
            "nested/settings.json", "cars/custom.res", "cars/custom.3sh", "tracks/custom.trk",
            "replays/custom.rpl", "scores/custom.hig"};
        for (String path : outputs) {
            fixture.put(path, LOCAL_CHANGE);
            fixture.engine().publish(path);
            fixture.requireContents(path, LOCAL_CHANGE);
            fixture.store.requireContents(path, LOCAL_CHANGE);
        }

        fixture.put("settings.cfg", IMPORTED);
        fixture.store.failWrites = true;
        rejects(() -> fixture.engine().publish("settings.cfg"), "Ignored failed config export.");
        fixture.requireContents("settings.cfg", IMPORTED);
        fixture.store.requireContents("settings.cfg", LOCAL_CHANGE);
        File pending = new File(fixture.state.getPath() + ".pending");
        require(pending.isDirectory(), "Failed output was not retained outside the game cache.");
        delete(fixture.game);
        fixture.store.failWrites = false;
        fixture.engine().refresh();
        fixture.requireContents("settings.cfg", IMPORTED);
        fixture.store.requireContents("settings.cfg", IMPORTED);

        fixture.put("new/nested.rpl", PUBLIC_CHANGE);
        fixture.store.failWrites = true;
        rejects(() -> fixture.engine().publish("new/nested.rpl"),
            "Ignored failed new file export.");
        fixture.requireContents("new/nested.rpl", PUBLIC_CHANGE);
        require(!fixture.store.files.containsKey("new/nested.rpl"), "Committed a failed write.");
        fixture.put("new/nested.rpl", IMPORTED);
        rejects(() -> fixture.engine().publish("new/nested.rpl"),
            "Ignored failure while replacing an older pending output.");
        fixture.requireContents("new/nested.rpl", IMPORTED);
        delete(fixture.game);
        fixture.store.failWrites = false;
        fixture.engine().refresh();
        fixture.requireContents("new/nested.rpl", IMPORTED);
        fixture.store.requireContents("new/nested.rpl", IMPORTED);
    }

    private static void providerFailureAndFullCache(File root) throws IOException {
        Fixture fixture = new Fixture(root, "provider-list-failure");
        fixture.engine().refresh();
        fixture.put("settings.cfg", LOCAL_CHANGE);
        fixture.store.failList = true;
        rejects(() -> fixture.engine().publish("settings.cfg"),
            "Ignored provider listing failure while publishing.");
        fixture.requireContents("settings.cfg", LOCAL_CHANGE);
        require(!fixture.store.files.containsKey("settings.cfg"),
            "Committed a failed publication.");
        delete(fixture.game);
        fixture.store.failList = false;
        fixture.engine().refresh();
        fixture.requireContents("settings.cfg", LOCAL_CHANGE);
        fixture.store.requireContents("settings.cfg", LOCAL_CHANGE);

        // Existing cache content at its limit must not stop capturing a completed new output.
        Fixture full = new Fixture(root, "full-cache");
        full.engine().refresh();
        for (int index = full.store.files.size(); index < GameDataSync.MAX_FILES; index++) {
            full.put("cache-content/file" + index + ".res", ORIGINAL);
        }
        full.put("new-save.rpl", IMPORTED);
        full.store.failWrites = true;
        rejects(() -> full.engine().publish("new-save.rpl"),
            "Ignored publication failure with a full cache.");
        full.requireContents("new-save.rpl", IMPORTED);
        delete(full.game);
        full.store.failWrites = false;
        full.engine().refresh();
        full.requireContents("new-save.rpl", IMPORTED);
        full.store.requireContents("new-save.rpl", IMPORTED);
        require(!new File(full.game, "cache-content").exists(),
            "Published unrelated cache content.");
    }

    private static void concurrentChange(File root) throws IOException {
        Fixture fixture = new Fixture(root, "concurrent-change");
        fixture.store.put("settings.cfg", ORIGINAL);
        fixture.store.put("scores/session.hig", ORIGINAL);
        fixture.engine().refresh();
        fixture.store.failWrites = true;
        fixture.put("scores/session.hig", LOCAL_CHANGE);
        rejects(() -> fixture.engine().publish("scores/session.hig"),
            "Ignored failure while queueing the first pending output.");
        fixture.put("settings.cfg", LOCAL_CHANGE);
        rejects(() -> fixture.engine().publish("settings.cfg"),
            "Ignored failure while queueing the second pending output.");
        fixture.store.failWrites = false;
        fixture.store.put("settings.cfg", PUBLIC_CHANGE);
        fixture.store.writes = 0;
        rejects(() -> fixture.engine().refresh(), "Discarded pending output during a conflict.");
        fixture.requireContents("settings.cfg", LOCAL_CHANGE);
        fixture.requireContents("scores/session.hig", LOCAL_CHANGE);
        fixture.store.requireContents("settings.cfg", PUBLIC_CHANGE);
        fixture.store.requireContents("scores/session.hig", ORIGINAL);
        require(fixture.store.writes == 0, "Partially published a conflicting pending queue.");
        fixture.store.put("settings.cfg", ORIGINAL);
        fixture.engine().refresh();
        fixture.requireContents("settings.cfg", LOCAL_CHANGE);
        fixture.store.requireContents("settings.cfg", LOCAL_CHANGE);
        fixture.requireContents("scores/session.hig", LOCAL_CHANGE);
        fixture.store.requireContents("scores/session.hig", LOCAL_CHANGE);
    }

    private static void folderReplacement(File root) throws IOException {
        Fixture first = new Fixture(root, "first-folder");
        first.store.put("first-car.res", ORIGINAL);
        first.store.put("old.rpl", ORIGINAL);
        first.engine().refresh();
        MemoryStore second = new MemoryStore();
        for (String path : REQUIRED_FILES) {
            second.put(path, PUBLIC_CHANGE);
        }
        second.put("misc.res", versionResource("gver", GameDataVersion.EXPECTED_VERSION));
        second.put("second-car.res", PUBLIC_CHANGE);
        File secondState = new File(first.root, "second-state");
        new GameDataSync(first.game, secondState, second).refresh();
        first.requireContents("main.res", PUBLIC_CHANGE);
        first.requireContents("second-car.res", PUBLIC_CHANGE);
        require(!new File(first.game, "old.rpl").exists(), "Kept files from the previously selected game folder.");
        require(!new File(first.game, "first-car.res").exists(), "Kept prior folder custom cars.");
        require(!second.files.containsKey("old.rpl"),
            "Published old files into a selected folder.");
        first.store.requireContents("old.rpl", ORIGINAL);
        require(first.store.writes == 0 && second.writes == 0,
            "Folder selection modified content.");
    }

    private static File zipRoot(Fixture fixture, String name, Map<String, byte[]> extra)
            throws IOException {
        return zipRoot(fixture, name, extra, Collections.emptySet());
    }

    private static File zipRoot(Fixture fixture, String name, Map<String, byte[]> extra,
            Set<String> directories) throws IOException {
        File staging = new File(fixture.root, name);
        GameDataImport.prepare(staging);
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        Map<String, byte[]> contents = new LinkedHashMap<>();
        for (String path : REQUIRED_FILES) {
            contents.put(path, ORIGINAL);
        }
        contents.put("misc.res", versionResource("gver", GameDataVersion.EXPECTED_VERSION));
        contents.putAll(extra);
        try (ZipOutputStream zip = new ZipOutputStream(bytes)) {
            for (String directory : directories) {
                zip.putNextEntry(new ZipEntry("OriginalGame/" + directory + "/"));
                zip.closeEntry();
            }
            for (Map.Entry<String, byte[]> entry : contents.entrySet()) {
                zip.putNextEntry(new ZipEntry("OriginalGame/" + entry.getKey()));
                zip.write(entry.getValue());
                zip.closeEntry();
            }
        }
        GameDataImport importer = new GameDataImport(staging);
        importer.readZip(new ByteArrayInputStream(bytes.toByteArray()));
        File source = importer.gameDirectory();
        GameDataVersion.validate(source);
        return source;
    }

    private static void zipMerge(File root) throws IOException {
        Fixture fixture = new Fixture(root, "zip-merge");
        fixture.store.put("keep.cfg", PUBLIC_CHANGE);
        fixture.store.put("cars/custom.res", ORIGINAL);
        fixture.engine().refresh();
        Map<String, byte[]> additions = new LinkedHashMap<>();
        additions.put("cars/custom.res", ORIGINAL);
        additions.put("cars/new.3sh", IMPORTED);
        additions.put("tracks/new.trk", IMPORTED);
        File source = zipRoot(fixture, "zip", additions);
        fixture.engine().installZip(source);
        fixture.engine().refresh();
        fixture.requireContents("cars/new.3sh", IMPORTED);
        fixture.requireContents("tracks/new.trk", IMPORTED);
        fixture.requireContents("keep.cfg", PUBLIC_CHANGE);
        fixture.store.requireContents("keep.cfg", PUBLIC_CHANGE);
        require(!fixture.store.files.containsKey("originalgame/main.res"),
            "Retained the ZIP wrapper folder in the selected game folder.");

        additions.put("cars/custom.res", LOCAL_CHANGE);
        additions.put("another/new.trk", LOCAL_CHANGE);
        File conflicting = zipRoot(fixture, "conflicting-zip", additions);
        fixture.store.writes = 0;
        rejects(() -> fixture.engine().installZip(conflicting),
            "Overwrote conflicting ZIP content.");
        fixture.store.requireContents("cars/custom.res", ORIGINAL);
        fixture.requireContents("cars/custom.res", ORIGINAL);
        require(!fixture.store.files.containsKey("another/new.trk"),
            "Partially published a conflicting ZIP.");
        require(fixture.store.writes == 0, "Attempted writes before ZIP conflict preflight.");

        additions.remove("cars/custom.res");
        File retry = zipRoot(fixture, "retry-zip", additions);
        fixture.store.failWrites = true;
        rejects(() -> fixture.engine().installZip(retry), "Ignored failed ZIP publication.");
        fixture.requireContents("keep.cfg", PUBLIC_CHANGE);
        fixture.requireContents("cars/custom.res", ORIGINAL);
        requireFile(new File(retry, "another/new.trk"), LOCAL_CHANGE);
        fixture.store.failWrites = false;
        fixture.engine().installZip(retry);
        fixture.engine().refresh();
        fixture.store.requireContents("another/new.trk", LOCAL_CHANGE);
    }

    private static void emptyDirectoriesAndCollisions(File root) throws IOException {
        Fixture fixture = new Fixture(root, "empty-directories");
        fixture.store.directory("Tracks");
        fixture.store.directory("Empty/Nested");
        fixture.engine().refresh();
        require(new File(fixture.game, "tracks").isDirectory(), "Lost the empty tracks folder.");
        require(new File(fixture.game, "empty/nested").isDirectory(), "Lost nested empty folders.");
        require(fixture.store.directoryWrites == 0, "Refresh created public directories.");
        fixture.put("tracks/new.trk", LOCAL_CHANGE);
        fixture.engine().publish("tracks/new.trk");
        fixture.store.requireContents("tracks/new.trk", LOCAL_CHANGE);

        fixture.store.files.remove("tracks/new.trk");
        fixture.store.directories.clear();
        fixture.engine().refresh();
        require(!new File(fixture.game, "tracks").exists(), "Resurrected a removed public folder.");
        require(!new File(fixture.game, "empty").exists(), "Retained removed empty folders.");

        Set<String> emptyFolders = new LinkedHashSet<>();
        emptyFolders.add("EmptyTracks/Nested");
        File source = zipRoot(fixture, "empty-folder-zip", Collections.emptyMap(), emptyFolders);
        fixture.engine().installZip(source);
        require(fixture.store.directories.contains("emptytracks/nested"),
            "ZIP publication skipped its empty folders.");
        fixture.engine().refresh();
        require(new File(fixture.game, "emptytracks/nested").isDirectory(),
            "ZIP refresh lost empty folders.");

        Fixture fileCollision = new Fixture(root, "public-file-directory-collision");
        fileCollision.store.put("occupied", ORIGINAL);
        fileCollision.engine().refresh();
        Map<String, byte[]> additions = new LinkedHashMap<>();
        additions.put("another/new.trk", LOCAL_CHANGE);
        emptyFolders.clear();
        emptyFolders.add("occupied/nested");
        File blocked = zipRoot(fileCollision, "blocked-zip", additions, emptyFolders);
        rejects(() -> fileCollision.engine().installZip(blocked),
            "Replaced a public file with a ZIP directory.");
        fileCollision.store.requireContents("occupied", ORIGINAL);
        fileCollision.requireContents("occupied", ORIGINAL);
        require(fileCollision.store.writes == 0 && fileCollision.store.directoryWrites == 0,
            "Modified the public folder before checking directory/file conflicts.");
        require(!fileCollision.store.files.containsKey("another/new.trk"),
            "Partially published a ZIP with directory/file conflicts.");

        Fixture directoryCollision = new Fixture(root, "public-directory-file-collision");
        directoryCollision.store.directory("occupied");
        directoryCollision.engine().refresh();
        additions.put("occupied", LOCAL_CHANGE);
        File blockedFile = zipRoot(directoryCollision, "blocked-file-zip", additions);
        rejects(() -> directoryCollision.engine().installZip(blockedFile),
            "Replaced a public directory with a ZIP file.");
        require(directoryCollision.store.directories.contains("occupied"),
            "Removed a conflicting public directory.");
        require(new File(directoryCollision.game, "occupied").isDirectory(),
            "Changed the cache after a ZIP directory/file conflict.");
        require(directoryCollision.store.writes == 0
                && directoryCollision.store.directoryWrites == 0,
            "Modified public content before checking the inverse directory/file conflict.");
    }

    private static void invalidDataPreservesCache(File root) throws IOException {
        Fixture fixture = new Fixture(root, "invalid-data");
        fixture.engine().refresh();
        byte[] originalVersion = fixture.store.files.get("misc.res").clone();
        byte[] state = Files.readAllBytes(fixture.state.toPath());
        byte[][] invalidVersions = {
            versionResource("gver", "Version 1.0 (Feb 12 1991)"),
            versionResource("nope", GameDataVersion.EXPECTED_VERSION),
            Arrays.copyOf(originalVersion, originalVersion.length - NUL_BYTES),
            ORIGINAL
        };
        for (byte[] invalid : invalidVersions) {
            fixture.store.put("misc.res", invalid);
            rejects(() -> fixture.engine().refresh(),
                "Installed invalid or incompatible game data.");
            fixture.requireContents("misc.res", originalVersion);
            require(Arrays.equals(state, Files.readAllBytes(fixture.state.toPath())),
                "Changed baseline while rejecting invalid game data.");
        }
        fixture.store.files.remove("misc.res");
        rejects(() -> fixture.engine().refresh(),
            "Installed game data without a version resource.");
        fixture.requireContents("misc.res", originalVersion);
        fixture.store.put("misc.res", originalVersion);
        for (String path : REQUIRED_FILES) {
            fixture.store.files.remove(path);
            rejects(() -> fixture.engine().refresh(), "Installed game data missing " + path);
            fixture.requireContents(path, ORIGINAL);
            fixture.store.put(path, ORIGINAL);
        }
        require(fixture.store.writes == 0, "Invalid-folder validation modified public files.");
    }

    private static void unsafePathsAndLimits(File root) throws IOException {
        Fixture fixture = new Fixture(root, "unsafe-paths");
        fixture.engine().refresh();
        fixture.store.put("../outside.res", PUBLIC_CHANGE);
        rejects(() -> fixture.engine().refresh(), "Accepted an escaping provider path.");
        fixture.requireContents("main.res", ORIGINAL);
        require(!new File(fixture.root, "outside.res").exists(),
            "Copied outside the cache folder.");
        fixture.store.files.remove("../outside.res");
        fixture.store.put("Cars/Custom.RES", ORIGINAL);
        fixture.store.put("cars/custom.res", ORIGINAL);
        rejects(() -> fixture.engine().refresh(), "Accepted duplicate public case aliases.");
        fixture.requireContents("main.res", ORIGINAL);
        require(fixture.store.writes == 0, "Unsafe refresh modified public content.");

        Fixture many = new Fixture(root, "many-files");
        many.engine().refresh();
        for (int index = 0; index <= GameDataSync.MAX_FILES; index++) {
            many.store.put("custom" + index + ".res", ORIGINAL);
        }
        rejects(() -> many.engine().refresh(), "Accepted too many public game files.");
        many.requireContents("main.res", ORIGINAL);

        Fixture large = new Fixture(root, "large-content");
        byte[] customVideo = new byte[CUSTOM_VIDEO_BYTES];
        customVideo[customVideo.length - 1] = PUBLIC_CHANGE[PUBLIC_CHANGE.length - 1];
        large.store.put("opponents/custom.webm", customVideo);
        large.engine().refresh();
        large.requireContents("opponents/custom.webm", customVideo);
        large.store.oversized = "too-large.webm";
        rejects(() -> large.engine().refresh(), "Accepted an oversized game folder.");
        large.requireContents("opponents/custom.webm", customVideo);
        require(!new File(large.game, "too-large.webm").exists(), "Installed oversized content.");
    }

    private static void delete(File root) throws IOException {
        try (Stream<Path> files = Files.walk(root.toPath())) {
            Path[] paths = files.sorted(Comparator.reverseOrder()).toArray(Path[]::new);
            for (Path path : paths) {
                Files.delete(path);
            }
        }
    }

    public static void main(String[] args) throws Exception {
        File root = Files.createTempDirectory("restunts-data-sync-test").toFile();
        try {
            paths();
            refreshAllContent(root);
            bundledHorizonAndOverride(root);
            failedRefresh(root);
            packedRefresh(root);
            publishEveryFormatAndRetry(root);
            providerFailureAndFullCache(root);
            concurrentChange(root);
            folderReplacement(root);
            zipMerge(root);
            emptyDirectoriesAndCollisions(root);
            invalidDataPreservesCache(root);
            unsafePathsAndLimits(root);
        } finally {
            delete(root);
        }
        System.out.println("Android full-folder refresh, removals, publish retry, ZIP and "
            + "validation passed.");
    }
}
