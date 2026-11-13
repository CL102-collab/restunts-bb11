package org.restunts.android;

import android.content.Context;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.HashSet;
import java.util.Set;

/** Read-only ROM folder access, isolated from the writable game folder and its saves. */
final class Mt32RomStorage {
    private static final String PREFERENCES = "mt32_roms";
    private static final String FOLDER_KEY = "folder";
    private static final String DIRECTORY = "mt32-roms";
    private static final Uri TV_FOLDER = Uri.parse("restunts:tv-mt32-rom-folder");
    private static final String STAGING = "mt32-roms-pending";
    private static final String BACKUP = "mt32-roms-previous";
    private static final int MAX_DEPTH = 16;
    private static final int MAX_ENTRIES = 4096;
    private static final long MAX_ROM_BYTES = 1024L * 1024L;
    private static final long MAX_TOTAL_BYTES = 64L * 1024L * 1024L;
    private static final int BUFFER_BYTES = 16384;
    private static final String[] COLUMNS = {
        DocumentsContract.Document.COLUMN_DOCUMENT_ID,
        DocumentsContract.Document.COLUMN_MIME_TYPE,
        DocumentsContract.Document.COLUMN_SIZE
    };

    private final Context context;
    private final Uri tree;
    private final File destination;
    private final Set<String> visited = new HashSet<>();
    private int entries;
    private int files;
    private long total;

    private Mt32RomStorage(Context context, Uri tree, File destination) {
        this.context = context;
        this.tree = tree;
        this.destination = destination;
    }

    static Uri folder(Context context) {
        String value = context.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE)
            .getString(FOLDER_KEY, "");
        return value.isEmpty() ? null : Uri.parse(value);
    }

    static File directory(Context context) {
        return new File(context.getFilesDir(), DIRECTORY);
    }

    static Uri tvFolder() {
        return TV_FOLDER;
    }

    static File tvDirectory(Context context) throws IOException {
        File result = new File(GameDataStorage.tvMediaDirectory(context), DIRECTORY);
        if (!result.getCanonicalFile().equals(result.getAbsoluteFile())) {
            throw new IOException("The TV ROM folder must not be a symbolic link.");
        }
        GameDataImport.makeDirectory(result);
        return result;
    }

    static String folderLabel(Context context) {
        Uri selected = folder(context);
        if (selected == null) {
            return "";
        }
        try {
            return TV_FOLDER.equals(selected) ? tvDirectory(context).getAbsolutePath()
                : DocumentsContract.getTreeDocumentId(selected);
        } catch (IOException | IllegalArgumentException error) {
            return selected.toString();
        }
    }

    static Mt32RomLibrary.Inventory select(Context context, Uri selected) throws IOException {
        Mt32RomLibrary.Inventory result = copy(context, selected);
        SharedPreferences preferences = context.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE);
        if (!preferences.edit().putString(FOLDER_KEY, selected.toString()).commit()) {
            throw new IOException("Cannot remember the MT-32 ROM folder.");
        }
        return result;
    }

    static Mt32RomLibrary.Inventory refresh(Context context) throws IOException {
        Uri selected = folder(context);
        if (selected == null) {
            throw new IOException("Choose your MT-32 ROM folder first.");
        }
        return copy(context, selected);
    }

    private static Mt32RomLibrary.Inventory copy(Context context, Uri selected) throws IOException {
        File staging = new File(context.getFilesDir(), STAGING);
        File previous = new File(context.getFilesDir(), BACKUP);
        File current = directory(context);
        // Recover a process termination between the two directory renames.
        if (!current.exists() && previous.isDirectory() && !previous.renameTo(current)) {
            throw new IOException("Cannot restore the previous MT-32 ROM cache.");
        }
        GameDataImport.prepare(staging);
        Mt32RomStorage importer = new Mt32RomStorage(context, selected, staging);
        try {
            if (TV_FOLDER.equals(selected)) {
                File tvRoot = tvDirectory(context);
                importer.readLocalDirectory(tvRoot, 0);
            } else {
                importer.readDirectory(DocumentsContract.getTreeDocumentId(selected), 0);
            }
            Mt32RomLibrary.Inventory result = Mt32RomLibrary.scan(staging.getAbsolutePath());
            GameDataImport.delete(previous);
            if (current.exists() && !current.renameTo(previous)) {
                throw new IOException("Cannot preserve the previous MT-32 ROM cache.");
            }
            if (!staging.renameTo(current)) {
                if (previous.exists()) {
                    previous.renameTo(current);
                }
                throw new IOException("Cannot install the MT-32 ROM cache.");
            }
            try {
                GameDataImport.delete(previous);
            } catch (IOException ignored) {
                // The next import can clean up an already replaced cache.
            }
            return result;
        } catch (RuntimeException error) {
            throw new IOException("Cannot read the selected MT-32 ROM folder.", error);
        } finally {
            if (staging.exists()) {
                GameDataImport.delete(staging);
            }
        }
    }

    private void readDirectory(String id, int depth) throws IOException {
        if (depth > MAX_DEPTH || !visited.add(id)) {
            throw new IOException("The MT-32 ROM folder is too deeply nested or contains a cycle.");
        }
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, id);
        try (Cursor cursor = context.getContentResolver().query(children, COLUMNS, null, null, null)) {
            if (cursor == null) {
                throw new IOException("Cannot list the MT-32 ROM folder.");
            }
            while (cursor.moveToNext()) {
                if (++entries > MAX_ENTRIES) {
                    throw new IOException("The MT-32 ROM folder contains too many entries.");
                }
                String childId = cursor.getString(0);
                if (childId == null) {
                    throw new IOException("The MT-32 ROM folder contains an unnamed entry.");
                }
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(cursor.getString(1))) {
                    readDirectory(childId, depth + 1);
                } else {
                    long size = cursor.isNull(2) ? -1 : cursor.getLong(2);
                    // Munt identifies contents, not names. Large unrelated files need not be cached.
                    if (size <= MAX_ROM_BYTES) {
                        copyFile(childId);
                    }
                }
            }
        }
    }

    private void readLocalDirectory(File directory, int depth) throws IOException {
        if (depth > MAX_DEPTH || !directory.getCanonicalFile().equals(directory.getAbsoluteFile())
                || !visited.add(directory.getCanonicalPath())) {
            throw new IOException("The TV ROM folder contains a link or is too deeply nested.");
        }
        File[] children = directory.listFiles();
        if (children == null) {
            throw new IOException("Cannot list the TV ROM folder.");
        }
        for (File child : children) {
            if (++entries > MAX_ENTRIES || !child.getCanonicalFile().equals(child.getAbsoluteFile())) {
                throw new IOException("The TV ROM folder contains too many files or a symbolic link.");
            }
            if (child.isDirectory()) {
                readLocalDirectory(child, depth + 1);
            } else if (child.isFile() && child.length() <= MAX_ROM_BYTES) {
                try (InputStream input = new FileInputStream(child)) {
                    cacheFile(input);
                }
            }
        }
    }

    private void copyFile(String id) throws IOException {
        if (!visited.add(id)) {
            throw new IOException("The MT-32 ROM folder contains repeated document IDs.");
        }
        Uri document = DocumentsContract.buildDocumentUriUsingTree(tree, id);
        try (InputStream input = context.getContentResolver().openInputStream(document)) {
            if (input == null) {
                throw new IOException("Cannot read a file in the MT-32 ROM folder.");
            }
            cacheFile(input);
        }
    }

    private void cacheFile(InputStream input) throws IOException {
        File target = new File(destination, "rom-" + files++ + ".bin");
        long bytes = 0;
        try (FileOutputStream output = new FileOutputStream(target)) {
            byte[] buffer = new byte[BUFFER_BYTES];
            int count;
            while ((count = input.read(buffer)) != -1) {
                bytes += count;
                if (bytes > MAX_ROM_BYTES) {
                    // A provider may not know its file sizes. Ignore oversized non-ROM files.
                    break;
                }
                total += count;
                if (total > MAX_TOTAL_BYTES) {
                    throw new IOException("The MT-32 ROM folder exceeds the import size limit.");
                }
                output.write(buffer, 0, count);
            }
        }
        if (bytes > MAX_ROM_BYTES && !target.delete()) {
            throw new IOException("Cannot discard an oversized file from the MT-32 ROM cache.");
        }
    }
}
