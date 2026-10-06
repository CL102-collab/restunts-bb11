package org.restunts.android;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FilterInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.TreeSet;

/** A fixed app-owned media folder using the same synchronization contract as SAF. */
public final class LocalGameFolder implements GameDataSync.Store {
    private static final String TEMP_PREFIX = ".restunts-save-";
    private static final String TEMP_SUFFIX = ".tmp";
    private static final String BACKUP_SUFFIX = ".bak";
    private final File root;
    private final Map<String, Entry> documents = new HashMap<>();
    private long totalBytes;

    private static final class Entry {
        final File file;
        final boolean directory;
        final long size;
        final long modified;

        Entry(File file) {
            this.file = file;
            directory = file.isDirectory();
            size = directory ? 0 : file.length();
            modified = file.lastModified();
        }
    }

    public LocalGameFolder(File directory) throws IOException {
        if (directory == null || directory.getAbsoluteFile().getParentFile() == null) {
            throw new IOException("The app's game folder is unavailable.");
        }
        File absolute = directory.getAbsoluteFile();
        root = absolute.getCanonicalFile();
        File expected = new File(absolute.getParentFile().getCanonicalFile(), absolute.getName());
        if (!root.equals(expected)) {
            throw new IOException("The game folder must not be a symbolic link.");
        }
        GameDataImport.makeDirectory(root);
        index(root, "", 0, new HashSet<>());
    }

    private static boolean temporaryPath(String path) {
        String name = path.substring(path.lastIndexOf('/') + 1);
        return name.startsWith(TEMP_PREFIX)
            && (name.endsWith(TEMP_SUFFIX) || name.endsWith(BACKUP_SUFFIX));
    }

    private static String checkedPath(String relativePath) throws IOException {
        String path = GameDataSync.normalizePath(relativePath);
        for (String component : path.split("/")) {
            if (temporaryPath(component)) {
                throw new IOException("This filename is reserved for game-folder transactions.");
            }
        }
        return path;
    }

    private void contained(File file) throws IOException {
        File canonical = file.getCanonicalFile();
        if (!canonical.equals(file.getAbsoluteFile()) || !(canonical.equals(root)
                || canonical.getPath().startsWith(root.getPath() + File.separator))) {
            throw new IOException("A game file escapes its root or is a symbolic link.");
        }
    }

    private static long checkedTotal(long previous, long size) throws IOException {
        if (size < 0 || size > GameDataSync.MAX_FILE_BYTES
                || size > GameDataSync.MAX_TOTAL_BYTES - previous) {
            throw new IOException("Game folder files exceed the storage limit.");
        }
        return previous + size;
    }

    private void add(String path, Entry entry) throws IOException {
        if (documents.containsKey(path) || documents.size() >= GameDataSync.MAX_FILES) {
            throw new IOException("Conflicting game filenames or too many documents: " + path);
        }
        if (!entry.directory) {
            totalBytes = checkedTotal(totalBytes, entry.size);
        }
        documents.put(path, entry);
    }

    private void index(File directory, String parent, int depth, Set<File> visited)
            throws IOException {
        contained(directory);
        if (!visited.add(directory.getCanonicalFile())) {
            throw new IOException("The game folder contains a directory cycle.");
        }
        File[] children = directory.listFiles();
        if (children == null) {
            throw new IOException("Cannot list the app's game folder.");
        }
        for (File child : children) {
            if (depth >= GameDataSync.MAX_DEPTH) {
                throw new IOException("The game folder is nested too deeply.");
            }
            contained(child);
            String name = GameDataSync.normalizePath(child.getName());
            if (name.indexOf('/') >= 0) {
                throw new IOException("Invalid game filename: " + child.getName());
            }
            String path = GameDataSync.normalizePath(parent + name);
            Entry entry = new Entry(child);
            if (!entry.directory && !child.isFile()) {
                throw new IOException("Unsupported game file: " + path);
            }
            if (temporaryPath(name)) {
                if (entry.directory) {
                    throw new IOException("A game subfolder uses a reserved transaction name.");
                }
                continue;
            }
            add(path, entry);
            if (entry.directory) {
                index(child, path + "/", depth + 1, visited);
            }
        }
    }

    @Override
    public synchronized Set<String> listFiles() {
        Set<String> result = new TreeSet<>();
        for (Map.Entry<String, Entry> document : documents.entrySet()) {
            if (!document.getValue().directory) {
                result.add(document.getKey());
            }
        }
        return result;
    }

    @Override
    public synchronized Set<String> listDirectories() {
        Set<String> result = new TreeSet<>();
        for (Map.Entry<String, Entry> document : documents.entrySet()) {
            if (document.getValue().directory) {
                result.add(document.getKey());
            }
        }
        return result;
    }

    private void unchanged(Entry entry) throws IOException {
        contained(entry.file);
        if (!entry.file.isFile() || entry.file.length() != entry.size
                || entry.file.lastModified() != entry.modified) {
            throw new IOException("The game folder changed during access; please retry.");
        }
    }

    @Override
    public synchronized InputStream read(String relativePath) throws IOException {
        String path = checkedPath(relativePath);
        Entry entry = documents.get(path);
        if (entry == null || entry.directory) {
            throw new IOException("Cannot find game file: " + path);
        }
        unchanged(entry);
        return new FilterInputStream(new FileInputStream(entry.file)) {
            private long bytes;

            private void count(int amount) throws IOException {
                if (amount > 0) {
                    bytes += amount;
                    if (bytes > GameDataSync.MAX_FILE_BYTES) {
                        throw new IOException("Game file exceeds the storage limit.");
                    }
                } else if (amount < 0) {
                    unchanged(entry);
                }
            }

            @Override
            public int read() throws IOException {
                int value = in.read();
                count(value < 0 ? -1 : 1);
                return value;
            }

            @Override
            public int read(byte[] buffer, int offset, int length) throws IOException {
                int amount = in.read(buffer, offset, length);
                count(amount);
                return amount;
            }

            @Override
            public void close() throws IOException {
                super.close();
                unchanged(entry);
            }
        };
    }

    private File child(File directory, String name) throws IOException {
        contained(directory);
        File[] children = directory.listFiles();
        if (children == null) {
            throw new IOException("Cannot read the app's game subfolder.");
        }
        File found = null;
        for (File candidate : children) {
            if (candidate.getName().equalsIgnoreCase(name)) {
                contained(candidate);
                if (found != null) {
                    throw new IOException("Conflicting game filenames: " + name);
                }
                found = candidate;
            }
        }
        return found;
    }

    private File directory(String path) throws IOException {
        File current = root;
        String prefix = "";
        for (String name : path.split("/")) {
            prefix = prefix.isEmpty() ? name : prefix + "/" + name;
            Entry entry = documents.get(prefix);
            File existing = child(current, name);
            if (entry != null) {
                if (!entry.directory || existing == null || !existing.equals(entry.file)) {
                    throw new IOException("A game subfolder changed or is occupied by a file: " + prefix);
                }
            } else {
                if (documents.size() >= GameDataSync.MAX_FILES) {
                    throw new IOException("The game folder contains too many documents.");
                }
                if (existing == null) {
                    existing = new File(current, name);
                    if (!existing.mkdir()) {
                        throw new IOException("Cannot create game subfolder: " + prefix);
                    }
                }
                contained(existing);
                if (!existing.isDirectory()) {
                    throw new IOException("A file occupies the game subfolder: " + prefix);
                }
                entry = new Entry(existing);
                add(prefix, entry);
            }
            current = entry.file;
        }
        return current;
    }

    @Override
    public synchronized void makeDirectory(String relativePath) throws IOException {
        directory(checkedPath(relativePath));
    }

    private void targetUnchanged(File directory, String name, Entry previous) throws IOException {
        File current = child(directory, name);
        if (previous == null) {
            if (current != null) {
                throw new IOException("A game file appeared during saving; please retry.");
            }
        } else {
            if (current == null || !current.equals(previous.file)) {
                throw new IOException("A game filename changed during saving; please retry.");
            }
            unchanged(previous);
        }
    }

    @Override
    public synchronized void write(String relativePath, InputStream input) throws IOException {
        if (input == null) {
            throw new IOException("Cannot read the completed game output.");
        }
        String path = checkedPath(relativePath);
        Entry previous = documents.get(path);
        if (previous != null && previous.directory) {
            throw new IOException("A directory occupies the game filename: " + path);
        }
        if (previous == null && documents.size() >= GameDataSync.MAX_FILES) {
            throw new IOException("The game folder contains too many documents.");
        }
        int separator = path.lastIndexOf('/');
        File parent = separator < 0 ? root : directory(path.substring(0, separator));
        if (previous == null && documents.size() >= GameDataSync.MAX_FILES) {
            throw new IOException("The game folder contains too many documents.");
        }
        String name = path.substring(separator + 1);
        targetUnchanged(parent, name, previous);
        File destination = previous == null ? new File(parent, name) : previous.file;
        long before = totalBytes - (previous == null ? 0 : previous.size);
        File temporary = File.createTempFile(TEMP_PREFIX, TEMP_SUFFIX, parent);
        IOException failure = null;
        try {
            contained(temporary);
            long bytes = 0;
            try (FileOutputStream output = new FileOutputStream(temporary)) {
                byte[] buffer = new byte[GameDataSync.BUFFER_SIZE];
                int count;
                while ((count = input.read(buffer)) != -1) {
                    bytes += count;
                    checkedTotal(before, bytes);
                    output.write(buffer, 0, count);
                }
                output.getFD().sync();
            }
            contained(temporary);
            if (!temporary.isFile() || temporary.length() != bytes) {
                throw new IOException("The temporary game file changed during saving; please retry.");
            }
            targetUnchanged(parent, name, previous);
            // Android's same-directory rename replaces atomically, preserving the
            // old target until the complete, synced replacement is committed.
            if (!temporary.renameTo(destination)) {
                throw new IOException("Cannot replace the game file; the previous copy is unchanged.");
            }
            documents.put(path, new Entry(destination));
            totalBytes = checkedTotal(before, bytes);
        } catch (IOException error) {
            failure = error;
            throw error;
        } finally {
            if (temporary.exists() && !temporary.delete()) {
                IOException cleanup = new IOException("Cannot remove a temporary game file.");
                if (failure != null) {
                    failure.addSuppressed(cleanup);
                } else {
                    throw cleanup;
                }
            }
        }
    }
}
