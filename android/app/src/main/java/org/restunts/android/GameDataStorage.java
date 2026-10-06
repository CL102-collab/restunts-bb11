package org.restunts.android;

import android.content.Context;
import android.content.SharedPreferences;
import android.content.UriPermission;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.widget.Toast;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.Set;

/** Serializes game-folder refreshes and native writes through the selected storage adapter. */
public final class GameDataStorage {
    private static final String PREFERENCES = "game_storage";
    private static final String FOLDER_KEY = "folder";
    private static final String ERROR_KEY = "last_error";
    private static final String STATE_DIRECTORY = "game-sync";
    private static final String GAME_DIRECTORY = "game";
    private static final String TV_GAME_DIRECTORY = "ChocolateStunts";
    private static final Uri TV_FOLDER = Uri.parse("restunts:tv-game-folder");
    private static final Handler MAIN_THREAD = new Handler(Looper.getMainLooper());

    private GameDataStorage() {}

    private static SharedPreferences preferences(Context context) {
        return context.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE);
    }

    public static Uri folder(Context context) {
        String value = preferences(context).getString(FOLDER_KEY, null);
        return value == null ? null : Uri.parse(value);
    }

    public static String lastError(Context context) {
        return preferences(context).getString(ERROR_KEY, "");
    }

    public static Uri tvFolder() {
        return TV_FOLDER;
    }

    public static boolean isTvFolder(Uri selected) {
        return TV_FOLDER.equals(selected);
    }

    /** Use only the primary app-owned shared directory; never request wider storage access. */
    public static File tvMediaDirectory(Context context) throws IOException {
        File[] directories = context.getExternalMediaDirs();
        if (directories == null || directories.length == 0 || directories[0] == null) {
            throw new IOException("The TV's shared storage is unavailable.");
        }
        File directory = directories[0].getCanonicalFile();
        GameDataImport.makeDirectory(directory);
        if (!directory.canRead() || !directory.canWrite()) {
            throw new IOException("The TV's app media folder cannot be read and written.");
        }
        return directory;
    }

    public static File tvGameDirectory(Context context) throws IOException {
        File directory = new File(tvMediaDirectory(context), TV_GAME_DIRECTORY);
        if (!directory.getCanonicalFile().equals(directory.getAbsoluteFile())) {
            throw new IOException("The TV game folder must not be a symbolic link.");
        }
        GameDataImport.makeDirectory(directory);
        return directory;
    }

    /** ZIP sources belong directly in the app-owned media root, outside the game content. */
    public static InputStream openTvZip(Context context, File source) throws IOException {
        File directory = tvMediaDirectory(context);
        File canonical = source.getCanonicalFile();
        if (!directory.equals(canonical.getParentFile()) || !canonical.isFile()) {
            throw new IOException("Copy the ZIP into the TV's app media folder first.");
        }
        return new FileInputStream(canonical);
    }

    private static GameDataSync.Store openFolder(Context context, Uri selected) throws IOException {
        if (isTvFolder(selected)) {
            return new LocalGameFolder(tvGameDirectory(context));
        }
        for (UriPermission permission : context.getContentResolver().getPersistedUriPermissions()) {
            if (selected.equals(permission.getUri()) && permission.isReadPermission()
                    && permission.isWritePermission()) {
                return new GameFolder(context, selected);
            }
        }
        throw new IOException("Game-folder access was removed. Choose the folder again.");
    }

    private static GameDataSync sync(Context context, Uri selected) {
        File state = new File(new File(context.getFilesDir(), STATE_DIRECTORY),
            GameDataSync.fingerprint(selected.toString()) + ".properties");
        // Delay provider access until after publish has captured the completed output.
        // Even a revoked grant must leave a durable pending copy for the next start.
        GameDataSync.Store store = new GameDataSync.Store() {
            private GameDataSync.Store opened;

            private GameDataSync.Store opened() throws IOException {
                if (opened == null) {
                    opened = openFolder(context, selected);
                }
                return opened;
            }

            @Override
            public Set<String> listFiles() throws IOException {
                return opened().listFiles();
            }

            @Override
            public Set<String> listDirectories() throws IOException {
                return opened().listDirectories();
            }

            @Override
            public void makeDirectory(String name) throws IOException {
                opened().makeDirectory(name);
            }

            @Override
            public InputStream read(String name) throws IOException {
                return opened().read(name);
            }

            @Override
            public void write(String name, InputStream input) throws IOException {
                opened().write(name, input);
            }
        };
        return new GameDataSync(new File(context.getFilesDir(), GAME_DIRECTORY), state, store);
    }

    public static synchronized void refresh(Context context) throws IOException {
        Uri selected = folder(context);
        if (selected == null) {
            throw new IOException("Choose your game folder or import a ZIP first.");
        }
        sync(context, selected).refresh();
        preferences(context).edit().remove(ERROR_KEY).apply();
    }

    private static void remember(Context context, Uri selected) throws IOException {
        if (!preferences(context).edit().putString(FOLDER_KEY, selected.toString())
                .remove(ERROR_KEY).commit()) {
            throw new IOException("Cannot remember the game folder. Please choose it again.");
        }
    }

    public static synchronized void select(Context context, Uri selected) throws IOException {
        sync(context, selected).refresh();
        remember(context, selected);
    }

    public static synchronized void importZip(Context context, Uri selected, File source)
            throws IOException {
        GameDataSync engine = sync(context, selected);
        engine.installZip(source);
        engine.refresh();
        remember(context, selected);
    }

    /** Any completed game output, including saves and screenshots, belongs to the same folder. */
    public static synchronized void publish(Context context, String path) {
        Uri selected = folder(context);
        if (selected == null) {
            return;
        }
        try {
            File game = new File(context.getFilesDir(), GAME_DIRECTORY).getCanonicalFile();
            String normalized = path.replace('\\', '/');
            File saved = new File(normalized);
            if (!saved.isAbsolute()) {
                saved = new File(game, normalized);
            }
            String prefix = game.getPath() + File.separator;
            String canonical = saved.getCanonicalPath();
            if (!canonical.startsWith(prefix)) {
                throw new IOException("Game output is outside the game folder.");
            }
            String relative = GameDataSync.normalizePath(canonical.substring(prefix.length()));
            sync(context, selected).publish(relative);
            preferences(context).edit().remove(ERROR_KEY).apply();
        } catch (IOException | RuntimeException error) {
            String message = "Saved on this device, but could not update your game folder: "
                + error.getMessage();
            preferences(context).edit().putString(ERROR_KEY, message).apply();
            MAIN_THREAD.post(() -> Toast.makeText(context, message, Toast.LENGTH_LONG).show());
        }
    }
}
