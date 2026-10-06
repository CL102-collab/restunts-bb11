package org.restunts.android;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.UriPermission;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.content.res.Configuration;
import android.net.Uri;
import android.os.Bundle;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.provider.DocumentsContract;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.lang.ref.WeakReference;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Choose one public game folder, validate a native working copy, then launch. */
public final class LauncherActivity extends Activity {
    private static final int REQUEST_FOLDER = 1;
    private static final int REQUEST_ZIP = 2;
    private static final int REQUEST_ZIP_FOLDER = 3;
    private static final String ZIP_DIRECTORY = "zip-import";
    private static final String ZIP_READY_KEY = "zip_ready";
    private static final String ZIP_TV_KEY = "zip_tv";
    private static final String TV_PICKER_STUB_PACKAGE = "com.android.tv.frameworkpackagestubs";
    private static final String INITIAL_URI_EXTRA = "android.provider.extra.INITIAL_URI";
    private static final String DOCUMENTS_URI =
        "content://com.android.externalstorage.documents/document/primary:Documents";
    private static final int PADDING_DP = 24;
    private static final int TV_PADDING_DP = 48;
    private static final int TEXT_SIZE_SP = 18;
    private static final int TV_TEXT_SIZE_SP = 22;
    private static final int TV_BUTTON_MINIMUM_WIDTH_DP = 360;
    private static final int OPTION_TEXT_SIZE_SP = 16;
    private static final int OPTION_SPACING_DP = 12;
    private static final String OPTIONS_PREFERENCES = "launch_options";
    // Jobs and their state outlive an Activity; only the main thread changes this state.
    private static final ExecutorService WORKER = Executors.newSingleThreadExecutor();
    private static final Handler MAIN_THREAD = new Handler(Looper.getMainLooper());
    private static WeakReference<LauncherActivity> active = new WeakReference<>(null);
    private static boolean importing;
    private static boolean synchronizing;
    private static boolean preparingAssets;
    private static boolean assetsReady;
    private static boolean permissionsCleaned;
    private static String currentStatus = "Import your Brøderbund Stunts 1.1 game files.\n"
        + "Choose the game folder itself, or a ZIP to extract into Chocolate Stunts.";
    private TextView status;
    private Button start;
    private Button folder;
    private Button zip;
    private Button tvFolder;
    private CheckBox newMidi;
    private CheckBox showFps;
    private Spinner hyperVision;
    private SharedPreferences preferences;
    private boolean television;

    @Override
    public void onCreate(Bundle state) {
        super.onCreate(state);
        television = (getResources().getConfiguration().uiMode & Configuration.UI_MODE_TYPE_MASK)
            == Configuration.UI_MODE_TYPE_TELEVISION;
        preferences = getSharedPreferences(OPTIONS_PREFERENCES, MODE_PRIVATE);
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setGravity(Gravity.CENTER);
        int padding = Math.round((television ? TV_PADDING_DP : PADDING_DP)
            * getResources().getDisplayMetrics().density);
        layout.setPadding(padding, padding, padding, padding);
        status = new TextView(this);
        status.setTextSize(television ? TV_TEXT_SIZE_SP : TEXT_SIZE_SP);
        status.setGravity(Gravity.CENTER);
        layout.addView(status);
        if (television) {
            TextView controls = new TextView(this);
            controls.setText("Use a keyboard or game controller for racing.");
            controls.setTextSize(TEXT_SIZE_SP);
            controls.setGravity(Gravity.CENTER);
            layout.addView(controls);
        }
        folder = button(layout, "Choose game folder", view -> chooseFolder());
        zip = button(layout, "Import Stunts ZIP", view -> chooseZip());
        if (television) {
            tvFolder = button(layout, "Use TV game folder", view -> showTvFolderDialog(false));
        }
        addOptions(layout);
        start = button(layout, "Start game", view -> startGame());
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.addView(layout, new ScrollView.LayoutParams(
            ScrollView.LayoutParams.MATCH_PARENT, ScrollView.LayoutParams.WRAP_CONTENT));
        setContentView(scroll);
        refresh();
        Context context = getApplicationContext();
        if (!permissionsCleaned) {
            permissionsCleaned = true;
            // A killed process cannot run the job's finally block. Its imports are not resumed.
            for (UriPermission previous : context.getContentResolver().getPersistedUriPermissions()) {
                if (previous.isReadPermission()
                        && !previous.getUri().equals(GameDataStorage.folder(context))) {
                    releaseFolderPermission(context, previous.getUri());
                }
            }
        }
        if (!preparingAssets && !assetsReady) {
            preparingAssets = true;
            boolean tv = television;
            WORKER.execute(() -> {
                try {
                    GameDataImport.recover(new File(context.getFilesDir(), "game"));
                    copyAssets(context, "", context.getFilesDir());
                    String readyStatus = initialStatus(context, tv);
                    MAIN_THREAD.post(() -> {
                        preparingAssets = false;
                        assetsReady = true;
                        if (!importing && !synchronizing) {
                            currentStatus = readyStatus;
                        }
                        refreshActive();
                    });
                } catch (IOException error) {
                    MAIN_THREAD.post(() -> {
                        preparingAssets = false;
                        currentStatus = "Cannot prepare game: " + error.getMessage();
                        refreshActive();
                    });
                }
            });
        }
    }

    @Override
    protected void onStart() {
        super.onStart();
        active = new WeakReference<>(this);
        String saveError = GameDataStorage.lastError(this);
        if (!saveError.isEmpty() && !importing && !synchronizing) {
            currentStatus = saveError + " Start game to retry copying your files.";
        }
        refresh();
    }

    @Override
    protected void onPause() {
        // Spinner callbacks can be deferred; persist the current widgets before leaving.
        saveOptions();
        super.onPause();
    }

    @Override
    protected void onStop() {
        if (active.get() == this) {
            active.clear();
        }
        super.onStop();
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        boolean colored = event.getKeyCode() == KeyEvent.KEYCODE_PROG_GREEN
            || event.getKeyCode() == KeyEvent.KEYCODE_PROG_RED;
        if (colored && event.getAction() == KeyEvent.ACTION_DOWN && event.getRepeatCount() != 0) {
            return true;
        }
        int keyCode;
        switch (event.getKeyCode()) {
            case KeyEvent.KEYCODE_BUTTON_A:
            case KeyEvent.KEYCODE_BUTTON_SELECT:
            case KeyEvent.KEYCODE_PROG_GREEN:
                keyCode = KeyEvent.KEYCODE_DPAD_CENTER;
                break;
            case KeyEvent.KEYCODE_BUTTON_B:
            case KeyEvent.KEYCODE_PROG_RED:
                keyCode = KeyEvent.KEYCODE_BACK;
                break;
            default:
                return super.dispatchKeyEvent(event);
        }
        return super.dispatchKeyEvent(new KeyEvent(event.getDownTime(), event.getEventTime(),
            event.getAction(), keyCode, event.getRepeatCount(), event.getMetaState(),
            event.getDeviceId(), event.getScanCode(), event.getFlags(), event.getSource()));
    }

    private Button button(LinearLayout layout, String label, View.OnClickListener action) {
        Button button = new Button(this);
        button.setText(label);
        button.setFocusable(true);
        if (television) {
            button.setMinWidth(Math.round(TV_BUTTON_MINIMUM_WIDTH_DP
                * getResources().getDisplayMetrics().density));
            button.setTextSize(TEXT_SIZE_SP);
        }
        button.setOnClickListener(action);
        layout.addView(button);
        return button;
    }

    private void addOptions(LinearLayout layout) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER);
        int textSize = television ? TEXT_SIZE_SP : OPTION_TEXT_SIZE_SP;
        int spacing = Math.round(OPTION_SPACING_DP * getResources().getDisplayMetrics().density);
        newMidi = new CheckBox(this);
        newMidi.setText("New MIDI");
        newMidi.setTextSize(textSize);
        newMidi.setSingleLine(true);
        newMidi.setChecked(preferences.getBoolean(LaunchOptions.NEW_MIDI_KEY,
            LaunchOptions.DEFAULT_NEW_MIDI));
        row.addView(newMidi);
        showFps = new CheckBox(this);
        showFps.setText("Show FPS");
        showFps.setTextSize(textSize);
        showFps.setSingleLine(true);
        showFps.setChecked(preferences.getBoolean(LaunchOptions.SHOW_FPS_KEY,
            LaunchOptions.DEFAULT_SHOW_FPS));
        LinearLayout.LayoutParams fpsLayout = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        fpsLayout.setMarginStart(spacing);
        row.addView(showFps, fpsLayout);
        TextView label = new TextView(this);
        label.setText("HyperVision");
        label.setTextSize(textSize);
        label.setSingleLine(true);
        LinearLayout.LayoutParams labelLayout = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        labelLayout.setMarginStart(spacing);
        row.addView(label, labelLayout);
        hyperVision = new Spinner(this);
        hyperVision.setId(View.generateViewId());
        label.setLabelFor(hyperVision.getId());
        ArrayAdapter<LaunchOptions.HyperVision> adapter = new ArrayAdapter<>(this,
            android.R.layout.simple_spinner_item, LaunchOptions.HyperVision.values());
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        hyperVision.setAdapter(adapter);
        hyperVision.setSelection(LaunchOptions.HyperVision.fromStored(
            preferences.getString(LaunchOptions.HYPERVISION_KEY, null)).ordinal());
        row.addView(hyperVision);
        HorizontalScrollView optionsScroll = new HorizontalScrollView(this);
        optionsScroll.setFillViewport(true);
        optionsScroll.addView(row, new HorizontalScrollView.LayoutParams(
            HorizontalScrollView.LayoutParams.WRAP_CONTENT, HorizontalScrollView.LayoutParams.WRAP_CONTENT));
        layout.addView(optionsScroll);
        newMidi.setOnCheckedChangeListener((button, checked) -> saveOptions());
        showFps.setOnCheckedChangeListener((button, checked) -> saveOptions());
        hyperVision.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                saveOptions();
            }

            @Override
            public void onNothingSelected(AdapterView<?> parent) {
                // A populated Spinner keeps its current choice.
            }
        });
    }

    private LaunchOptions selectedOptions() {
        return new LaunchOptions(newMidi.isChecked(), showFps.isChecked(),
            (LaunchOptions.HyperVision) hyperVision.getSelectedItem());
    }

    private void saveOptions() {
        LaunchOptions options = selectedOptions();
        preferences.edit().putBoolean(LaunchOptions.NEW_MIDI_KEY, options.newMidi)
            .putBoolean(LaunchOptions.SHOW_FPS_KEY, options.showFps)
            .putString(LaunchOptions.HYPERVISION_KEY, options.hyperVision.name()).apply();
    }

    private void refresh() {
        status.setText(currentStatus);
        boolean busy = importing || synchronizing || preparingAssets;
        folder.setEnabled(!busy);
        zip.setEnabled(!busy);
        if (tvFolder != null) {
            tvFolder.setEnabled(!busy);
        }
        zip.setText(zipReady(this) ? "Continue ZIP import" : "Import Stunts ZIP");
        folder.setText(GameDataStorage.folder(this) == null ? "Choose game folder" : "Change game folder");
        start.setEnabled(!busy && assetsReady && GameDataStorage.folder(this) != null);
        View focused = getCurrentFocus();
        if (television && (focused == null || !focused.isEnabled())) {
            (start.isEnabled() ? start : folder).requestFocus();
        }
    }

    private static void refreshActive() {
        LauncherActivity activity = active.get();
        if (activity != null) {
            activity.refresh();
        }
    }

    private void startGame() {
        if (importing || synchronizing || preparingAssets) {
            return;
        }
        LaunchOptions options = selectedOptions();
        Context context = getApplicationContext();
        synchronizing = true;
        currentStatus = "Loading and validating your game folder…";
        refreshActive();
        WORKER.execute(() -> {
            try {
                GameDataStorage.refresh(context);
                MAIN_THREAD.post(() -> {
                    synchronizing = false;
                    currentStatus = "Ready to play.";
                    refreshActive();
                    LauncherActivity activity = active.get();
                    if (activity != null) {
                        Intent intent = new Intent(activity, GameActivity.class);
                        intent.putExtra(LaunchOptions.NEW_MIDI_KEY, options.newMidi);
                        intent.putExtra(LaunchOptions.SHOW_FPS_KEY, options.showFps);
                        intent.putExtra(LaunchOptions.HYPERVISION_KEY, options.hyperVision.name());
                        activity.startActivity(intent);
                    }
                });
            } catch (IOException | RuntimeException error) {
                MAIN_THREAD.post(() -> {
                    synchronizing = false;
                    // Leave Start available so a transient provider error can be retried.
                    currentStatus = "Cannot prepare game folder: " + error.getMessage();
                    refreshActive();
                });
            }
        });
    }

    private void selectGameFolder(Intent data, boolean zipImport) {
        Context context = getApplicationContext();
        Uri selected = data.getData();
        int permissions = Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION;
        if ((data.getFlags() & permissions) != permissions) {
            currentStatus = "Choose a folder that allows reading and writing game files.";
            refresh();
            return;
        }
        boolean alreadyGranted = false;
        for (UriPermission permission : context.getContentResolver().getPersistedUriPermissions()) {
            if (selected.equals(permission.getUri()) && permission.isReadPermission()
                    && permission.isWritePermission()) {
                alreadyGranted = true;
            }
        }
        try {
            context.getContentResolver().takePersistableUriPermission(selected, permissions);
        } catch (SecurityException error) {
            currentStatus = "Cannot keep access to the game folder: " + error.getMessage();
            refresh();
            return;
        }
        boolean acquired = !alreadyGranted;
        useGameFolder(selected, zipImport, acquired);
    }

    private void useGameFolder(Uri selected, boolean zipImport, boolean acquired) {
        if (importing || synchronizing) {
            if (acquired) {
                releaseFolderPermission(getApplicationContext(), selected);
            }
            return;
        }
        Context context = getApplicationContext();
        Uri previous = GameDataStorage.folder(context);
        importing = true;
        currentStatus = zipImport ? "Extracting ZIP into your selected game folder…"
            : "Loading and validating the selected game folder…";
        refreshActive();
        WORKER.execute(() -> {
            try {
                if (zipImport) {
                    GameDataImport importer = new GameDataImport(
                        new File(context.getFilesDir(), ZIP_DIRECTORY));
                    GameDataStorage.importZip(context, selected, importer.gameDirectory());
                    context.getSharedPreferences(OPTIONS_PREFERENCES, MODE_PRIVATE).edit()
                        .putBoolean(ZIP_READY_KEY, false).remove(ZIP_TV_KEY).apply();
                    try {
                        GameDataImport.delete(new File(context.getFilesDir(), ZIP_DIRECTORY));
                    } catch (IOException cleanup) {
                        // Import already committed. Unused staging must not revoke its grant.
                    }
                } else {
                    GameDataStorage.select(context, selected);
                }
                if (previous != null && !previous.equals(selected)) {
                    releaseFolderPermission(context, previous);
                }
                String message = GameDataStorage.isTvFolder(selected)
                    ? "TV game folder: " + GameDataStorage.tvGameDirectory(context).getPath()
                        + "\nCustom content and all saves belong in this folder."
                    : "Game folder selected. Custom cars and all saves belong in this folder.";
                MAIN_THREAD.post(() -> {
                    importing = false;
                    currentStatus = message;
                    refreshActive();
                });
            } catch (IOException | RuntimeException error) {
                if (acquired && !selected.equals(GameDataStorage.folder(context))) {
                    releaseFolderPermission(context, selected);
                }
                MAIN_THREAD.post(() -> {
                    importing = false;
                    currentStatus = "Cannot use game folder: " + error.getMessage();
                    refreshActive();
                });
            }
        });
    }

    private static void releaseFolderPermission(Context context, Uri uri) {
        if (GameDataStorage.isTvFolder(uri)) {
            return;
        }
        try {
            context.getContentResolver().releasePersistableUriPermission(uri,
                Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        } catch (SecurityException error) {
            // It may have been revoked already, or only a temporary grant was offered.
        }
    }

    private void chooseFolder() {
        chooseFolder(REQUEST_FOLDER);
    }

    private void chooseFolder(int request) {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION
            | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION | Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            Uri selected = GameDataStorage.folder(this);
            intent.putExtra(INITIAL_URI_EXTRA, selected == null || GameDataStorage.isTvFolder(selected)
                ? Uri.parse(DOCUMENTS_URI) : selected);
        }
        openPicker(intent, request,
            "No folder picker is available. Install a document picker to choose your game folder.");
    }

    private static boolean zipReady(Context context) {
        return context.getSharedPreferences(OPTIONS_PREFERENCES, MODE_PRIVATE)
            .getBoolean(ZIP_READY_KEY, false)
            && new File(context.getFilesDir(), ZIP_DIRECTORY).isDirectory();
    }

    private void chooseZip() {
        if (zipReady(this)) {
            new AlertDialog.Builder(this).setTitle("Game ZIP ready")
                .setItems(new String[] {"Choose destination folder", "Choose another ZIP"},
                    (dialog, selected) -> {
                        if (selected == 0) {
                            showZipDestinationDialog();
                        } else {
                            chooseZipFile();
                        }
                    }).show();
        } else {
            chooseZipFile();
        }
    }

    private void showZipDestinationDialog() {
        if (getSharedPreferences(OPTIONS_PREFERENCES, MODE_PRIVATE).getBoolean(ZIP_TV_KEY, false)) {
            showTvFolderDialog(true);
            return;
        }
        new AlertDialog.Builder(this).setTitle("Choose ZIP extraction folder")
            .setMessage("Next, select the target folder where the ZIP will be extracted.\n\n"
                + "Create or select Documents/Chocolate Stunts, or another folder. "
                + "This will be your game folder.")
            .setPositiveButton(android.R.string.ok,
                (dialog, selected) -> chooseFolder(REQUEST_ZIP_FOLDER))
            .show();
    }

    private void chooseZipFile() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {"application/zip", "application/x-zip-compressed"});
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        openPicker(intent, REQUEST_ZIP,
            "No file picker is available. Install a document picker to import your game files.");
    }

    private void openPicker(Intent intent, int request, String unavailableMessage) {
        try {
            if (television) {
                List<ResolveInfo> candidates = getPackageManager().queryIntentActivities(intent,
                    PackageManager.MATCH_DEFAULT_ONLY);
                ResolveInfo usable = null;
                for (ResolveInfo candidate : candidates) {
                    if (candidate.activityInfo == null || !candidate.activityInfo.exported
                            || TV_PICKER_STUB_PACKAGE.equals(candidate.activityInfo.packageName)) {
                        continue;
                    }
                    String permission = candidate.activityInfo.permission;
                    if (permission == null || getPackageManager().checkPermission(permission,
                            getPackageName()) == PackageManager.PERMISSION_GRANTED) {
                        usable = candidate;
                        break;
                    }
                }
                if (usable == null) {
                    showTvFallback(request);
                    return;
                }
                // A TV placeholder may resolve successfully and then show its own failure toast.
                intent.setComponent(new ComponentName(usable.activityInfo.packageName,
                    usable.activityInfo.name));
            }
            startActivityForResult(intent, request);
        } catch (ActivityNotFoundException error) {
            if (television) {
                showTvFallback(request);
            } else {
                currentStatus = unavailableMessage;
                refresh();
            }
        } catch (SecurityException error) {
            if (television) {
                showTvFallback(request);
            } else {
                currentStatus = "Cannot open the document picker: " + error.getMessage();
                refresh();
            }
        }
    }

    private static String initialStatus(Context context, boolean television) {
        Uri selected = GameDataStorage.folder(context);
        if ((television && selected == null) || GameDataStorage.isTvFolder(selected)) {
            try {
                File game = GameDataStorage.tvGameDirectory(context);
                return "Import your Brøderbund Stunts 1.1 game files.\n"
                    + "Copy a ZIP to: " + GameDataStorage.tvMediaDirectory(context).getPath()
                    + "\nTV game folder: " + game.getPath()
                    + "\nThis app-owned folder is removed when the app is uninstalled.";
            } catch (IOException | RuntimeException error) {
                return "The TV game folder is unavailable: " + error.getMessage();
            }
        }
        return selected == null ? "Choose your Brøderbund Stunts 1.1 game folder, or import a ZIP."
            : "Game folder remembered. Start game to load its current contents.";
    }

    private void showTvFallback(int request) {
        if (request == REQUEST_ZIP) {
            chooseTvZipFile();
        } else {
            showTvFolderDialog(request == REQUEST_ZIP_FOLDER);
        }
    }

    private void showTvFolderDialog(boolean zipImport) {
        if (importing || synchronizing || preparingAssets) {
            return;
        }
        try {
            File game = GameDataStorage.tvGameDirectory(this);
            String instruction = zipImport ? "Extract the validated ZIP into this folder?"
                : "Copy your extracted Brøderbund Stunts 1.1 files into this folder, then select it. "
                    + "To import a ZIP, copy it to " + GameDataStorage.tvMediaDirectory(this).getPath()
                    + " and press Choose TV ZIP.";
            AlertDialog.Builder dialog = new AlertDialog.Builder(this).setTitle("TV game folder")
                .setMessage(instruction + "\n\n" + game.getPath()
                    + "\n\nCustom content and all saves use this folder. No storage permission is needed. "
                    + "Uninstalling the app deletes this folder; back up your files first.")
                .setPositiveButton(zipImport ? "Extract ZIP here" : "Use this folder",
                    (choice, selected) -> useGameFolder(GameDataStorage.tvFolder(), zipImport, false))
                .setNegativeButton(android.R.string.cancel, null);
            if (!zipImport) {
                dialog.setNeutralButton("Choose TV ZIP", (choice, selected) -> chooseTvZipFile());
            }
            dialog.show();
        } catch (IOException | RuntimeException error) {
            currentStatus = "Cannot use the TV game folder: " + error.getMessage();
            refresh();
        }
    }

    private void chooseTvZipFile() {
        if (importing || synchronizing || preparingAssets) {
            return;
        }
        Context context = getApplicationContext();
        importing = true;
        currentStatus = "Looking for ZIP files in the TV's app media folder…";
        refreshActive();
        WORKER.execute(() -> {
            try {
                File directory = GameDataStorage.tvMediaDirectory(context);
                File[] children = directory.listFiles();
                if (children == null || children.length > GameDataSync.MAX_FILES) {
                    throw new IOException("Cannot list the TV import folder, or it contains too many files.");
                }
                ArrayList<File> choices = new ArrayList<>();
                for (File child : children) {
                    if (child.isFile() && child.getName().toLowerCase(Locale.ROOT).endsWith(".zip")
                            && directory.equals(child.getCanonicalFile().getParentFile())) {
                        choices.add(child);
                    }
                }
                File[] files = choices.toArray(new File[0]);
                Arrays.sort(files, (left, right) -> left.getName().compareToIgnoreCase(right.getName()));
                MAIN_THREAD.post(() -> {
                    importing = false;
                    currentStatus = "Copy your Stunts 1.1 ZIP to: " + directory.getPath()
                        + "\nChoose Import Stunts ZIP again after copying it.";
                    refreshActive();
                    LauncherActivity activity = active.get();
                    if (activity != null) {
                        activity.showTvZipFiles(directory, files);
                    }
                });
            } catch (IOException | RuntimeException error) {
                MAIN_THREAD.post(() -> {
                    importing = false;
                    currentStatus = "Cannot read the TV import folder: " + error.getMessage();
                    refreshActive();
                });
            }
        });
    }

    private void showTvZipFiles(File directory, File[] files) {
        AlertDialog.Builder dialog = new AlertDialog.Builder(this).setTitle("Import TV game ZIP");
        if (files.length == 0) {
            dialog.setMessage("Copy your Brøderbund Stunts 1.1 ZIP into this folder, then choose "
                + "Import Stunts ZIP again:\n\n" + directory.getPath())
                .setPositiveButton(android.R.string.ok, null);
        } else {
            String[] names = new String[files.length];
            for (int index = 0; index < files.length; index++) {
                names[index] = files[index].getName();
            }
            dialog.setItems(names, (choice, selected) -> readGameZip(Uri.fromFile(files[selected]), false, true))
                .setNegativeButton(android.R.string.cancel, null);
        }
        dialog.show();
    }

    /** Return true only for a grant acquired by this job, which it must release. */
    private static boolean retainReadPermission(Context context, Uri uri, int flags) {
        int permission = flags & Intent.FLAG_GRANT_READ_URI_PERMISSION;
        if (permission == 0 || (flags & Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION) == 0) {
            return false;
        }
        for (UriPermission previous : context.getContentResolver().getPersistedUriPermissions()) {
            if (uri.equals(previous.getUri()) && previous.isReadPermission()) {
                return false;
            }
        }
        try {
            context.getContentResolver().takePersistableUriPermission(uri, permission);
            return true;
        } catch (SecurityException error) {
            // Some document providers offer only the temporary Activity grant.
            return false;
        }
    }

    private static void releaseReadPermission(Context context, Uri uri) {
        try {
            context.getContentResolver().releasePersistableUriPermission(uri,
                Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (SecurityException error) {
            // A provider may revoke its grant independently while the job runs.
        }
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (result != RESULT_OK || data == null || data.getData() == null || importing || synchronizing) {
            return;
        }
        if (request == REQUEST_FOLDER || request == REQUEST_ZIP_FOLDER) {
            selectGameFolder(data, request == REQUEST_ZIP_FOLDER);
            return;
        }
        if (request != REQUEST_ZIP) {
            return;
        }
        Uri uri = data.getData();
        Context context = getApplicationContext();
        boolean retainedGrant = retainReadPermission(context, uri, data.getFlags());
        readGameZip(uri, retainedGrant, false);
    }

    private void readGameZip(Uri uri, boolean retainedGrant, boolean tvImport) {
        Context context = getApplicationContext();
        if (importing || synchronizing) {
            if (retainedGrant) {
                releaseReadPermission(context, uri);
            }
            return;
        }
        importing = true;
        currentStatus = "Reading and validating game ZIP…";
        refreshActive();
        WORKER.execute(() -> {
            try {
                SharedPreferences importPreferences = context.getSharedPreferences(
                    OPTIONS_PREFERENCES, MODE_PRIVATE);
                if (!importPreferences.edit().putBoolean(ZIP_READY_KEY, false).commit()) {
                    throw new IOException("Cannot remember the ZIP import state.");
                }
                File staging = new File(context.getFilesDir(), ZIP_DIRECTORY);
                GameDataImport.prepare(staging);
                GameDataImport importer = new GameDataImport(staging);
                try (InputStream input = tvImport
                        ? GameDataStorage.openTvZip(context, new File(uri.getPath()))
                        : context.getContentResolver().openInputStream(uri)) {
                    importer.readZip(input);
                }
                importer.gameDirectory();
                if (!importPreferences.edit().putBoolean(ZIP_READY_KEY, true)
                        .putBoolean(ZIP_TV_KEY, tvImport).commit()) {
                    throw new IOException("Cannot remember the ZIP import state.");
                }
                MAIN_THREAD.post(() -> {
                    importing = false;
                    currentStatus = tvImport ? "ZIP validated. Choose the TV game folder for extraction."
                        : "ZIP validated. Create or select Documents/Chocolate Stunts as its game folder.";
                    refreshActive();
                    LauncherActivity activity = active.get();
                    if (activity != null) {
                        activity.showZipDestinationDialog();
                    }
                });
            } catch (IOException | RuntimeException error) {
                MAIN_THREAD.post(() -> {
                    importing = false;
                    currentStatus = "ZIP import failed: " + error.getMessage();
                    refreshActive();
                });
            } finally {
                if (retainedGrant) {
                    releaseReadPermission(context, uri);
                }
            }
        });
    }

    private static void copyAssets(Context context, String path, File destination) throws IOException {
        String[] children = context.getAssets().list(path);
        if (children != null && children.length != 0) {
            GameDataImport.makeDirectory(destination);
            for (String child : children) {
                copyAssets(context, path.isEmpty() ? child : path + "/" + child, new File(destination, child));
            }
        } else {
            GameDataImport.makeDirectory(destination.getParentFile());
            try (InputStream input = context.getAssets().open(path);
                 FileOutputStream output = new FileOutputStream(destination)) {
                byte[] buffer = new byte[GameDataImport.BUFFER_SIZE];
                int count;
                while ((count = input.read(buffer)) != -1) {
                    output.write(buffer, 0, count);
                }
            }
        }
    }
}
