package org.restunts.genghost;

import android.Manifest;
import android.annotation.SuppressLint;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.InputType;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.lang.ref.WeakReference;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Locale;
import java.util.Set;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import org.restunts.android.GameDataStorage;

/** A document-folder launcher for the background, headless ghost generator. */
public final class GenghostActivity extends Activity {
    private static final int REQUEST_FOLDER = 1;
    private static final int REQUEST_EXPORT = 2;
    private static final int REQUEST_NOTIFICATIONS = 3;
    private static final String OPTIONS = "genghost_options";
    private static final String[] MODES = {GenerationRequest.MODE_GENERATE,
        GenerationRequest.MODE_FINE_TUNE, GenerationRequest.MODE_GEARS_ONLY};
    private static final String[] MODE_LABELS = {"Generate replay", "Fine-tune replay",
        "Fine-tune gears only"};
    private static final String[] STRATEGIES = {"legacy", "state"};
    private static final String[] STRATEGY_LABELS = {"Route search", "State search (preview)"};
    private static final ExecutorService FILE_WORKER = Executors.newSingleThreadExecutor();
    private static final Handler MAIN_THREAD = new Handler(Looper.getMainLooper());
    private static WeakReference<GenghostActivity> active = new WeakReference<>(null);
    private static List<String> availableTracks = Collections.emptyList();
    private static List<String> availableCars = Collections.emptyList();
    private static boolean folderBusy;
    private static boolean catalogReady;
    private static boolean exporting;
    private static String folderMessage = "Choose your Stunts 1.1 game folder. It should contain "
        + "the original game data, cars, and your .TRK or .RPL files.";
    private SharedPreferences preferences;
    private TextView folderStatus;
    private TextView runStatus;
    private TextView log;
    private TextView carLabel;
    private Spinner track;
    private Spinner car;
    private Spinner mode;
    private Spinner strategy;
    private EditText minutes;
    private EditText cores;
    private CheckBox powerGear;
    private CheckBox collision;
    private CheckBox leftBias;
    private CheckBox dualWaySwitching;
    private CheckBox owoot;
    private CheckBox amigaGrass;
    private Button chooseFolder;
    private Button refreshFolder;
    private Button generate;
    private Button cancel;
    private Button export;
    private Button exportReport;
    private boolean receiverRegistered;
    private String exportSource;
    private GeneratorService.State generation;

    private final BroadcastReceiver stateReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            refreshState();
        }
    };

    private final Runnable ticker = new Runnable() {
        @Override
        public void run() {
            refreshState();
            MAIN_THREAD.postDelayed(this, 1000L);
        }
    };

    @Override
    public void onCreate(Bundle saved) {
        super.onCreate(saved);
        preferences = getSharedPreferences(OPTIONS, MODE_PRIVATE);
        exportSource = saved == null ? null : saved.getString("export_source");
        LinearLayout page = new LinearLayout(this);
        page.setOrientation(LinearLayout.VERTICAL);
        page.setBackgroundResource(R.color.genghost_background);
        page.setPadding(dp(24), dp(28), dp(24), dp(28));
        page.setFitsSystemWindows(true);
        TextView title = text(page, "Genghost", 32);
        title.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        title.setTextColor(Color.rgb(115, 226, 209));
        text(page, "Build a faster ghost", 19);
        text(page, "Generate a ghost from a track and car, or fine-tune a completed replay "
            + "using its recorded car. The search runs in the background.", 15);

        label(page, "Game folder");
        folderStatus = text(page, folderMessage, 15);
        LinearLayout folderButtons = row(page);
        chooseFolder = button(folderButtons, "Choose folder", view -> chooseGameFolder(), true);
        refreshFolder = button(folderButtons, "Refresh", view -> updateFolder(null), true);

        label(page, "Track or replay");
        track = spinner(page, Collections.emptyList());
        label(page, "Search mode");
        mode = spinner(page, Arrays.asList(MODE_LABELS));
        String rememberedMode = preferences.getString("mode",
            preferences.getBoolean("fine_tune", false)
                ? GenerationRequest.MODE_FINE_TUNE : GenerationRequest.MODE_GENERATE);
        mode.setSelection(Math.max(0, Arrays.asList(MODES).indexOf(rememberedMode)));
        text(page, "Fine-tuning requires a completed .RPL and uses its recorded car. "
            + "Gears only changes shift timing. All generated replays use manual transmission.", 14);
        carLabel = label(page, "Car");
        car = spinner(page, Collections.emptyList());
        label(page, "Search time (minutes)");
        minutes = new EditText(this);
        minutes.setSingleLine(true);
        minutes.setInputType(InputType.TYPE_CLASS_NUMBER);
        minutes.setText(preferences.getString("minutes", "30"));
        minutes.setSelectAllOnFocus(true);
        page.addView(minutes, fullWidth());
        text(page, "The best completed replay is kept as the search improves it.", 14);
        label(page, "CPU cores");
        cores = new EditText(this);
        cores.setSingleLine(true);
        cores.setInputType(InputType.TYPE_CLASS_NUMBER);
        cores.setText(preferences.getString("cores",
            Integer.toString(GenerationRequest.DEFAULT_CORES)));
        cores.setSelectAllOnFocus(true);
        page.addView(cores, fullWidth());
        text(page, "1 uses one core; 0 uses all available cores. More cores use more memory "
            + "and battery.", 14);
        label(page, "Search method");
        strategy = spinner(page, Arrays.asList(STRATEGY_LABELS));
        strategy.setSelection(Math.max(0, Arrays.asList(STRATEGIES).indexOf(
            preferences.getString("strategy", GenerationRequest.DEFAULT_STRATEGY))));

        label(page, "Driving rules");
        LinearLayout drivingRules = new LinearLayout(this);
        drivingRules.setOrientation(LinearLayout.VERTICAL);
        powerGear = checkbox(drivingRules, "Power Gear", "power_gear", true);
        collision = checkbox(drivingRules, "Original Collision Bugs", "collision", true);
        leftBias = checkbox(drivingRules, "Left Corner Bias", "left_bias", true);
        dualWaySwitching = checkbox(drivingRules, "Dual-Way Switching", "dual_way_switching", true);
        owoot = checkbox(drivingRules, "OWOOT", "owoot", false);
        owoot.setContentDescription("One Wheel On / Over Track (OWOOT)");
        amigaGrass = checkbox(drivingRules, "Amiga Grass", "amiga_grass", false);
        page.addView(drivingRules, fullWidth());
        text(page, "Use the same driving rules when playing the saved replay.", 14);

        generate = button(page, "Generate ghost", view -> startGeneration(), false);
        runStatus = text(page, "Ready to generate a replay.", 16);
        cancel = button(page, "Stop and keep best replay", view -> {
            GeneratorService.cancel(this);
            refreshState();
        }, false);
        LinearLayout outputButtons = row(page);
        export = button(outputButtons, "Export replay", view -> chooseExport(false), true);
        exportReport = button(outputButtons, "Export report", view -> chooseExport(true), true);
        text(page, "Finished replays and their driving-rule reports are also copied to your "
            + "game folder.", 14);
        label(page, "Search log");
        log = text(page, "", 12);
        log.setTypeface(Typeface.MONOSPACE);
        log.setTextIsSelectable(true);
        ScrollView scroll = new ScrollView(this);
        // Paint every exposed viewport pixel instead of relying on the window background.
        scroll.setBackgroundResource(R.color.genghost_background);
        scroll.setFillViewport(true);
        scroll.setFitsSystemWindows(true);
        scroll.addView(page);
        setContentView(scroll);
        track.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                refreshState();
            }

            @Override
            public void onNothingSelected(AdapterView<?> parent) {
                refreshState();
            }
        });
        mode.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                refreshState();
            }

            @Override
            public void onNothingSelected(AdapterView<?> parent) {
                refreshState();
            }
        });
        populateCatalog();
        refreshState();
    }

    @Override
    protected void onStart() {
        super.onStart();
        active = new WeakReference<>(this);
        IntentFilter filter = new IntentFilter(GeneratorService.ACTION_STATE);
        if (Build.VERSION.SDK_INT >= 33) {
            registerReceiver(stateReceiver, filter, Context.RECEIVER_NOT_EXPORTED);
        } else {
            registerLegacyStateReceiver(filter);
        }
        receiverRegistered = true;
        MAIN_THREAD.post(ticker);
        if (!catalogReady && !folderBusy && GameDataStorage.folder(this) != null) {
            updateFolder(null);
        } else {
            populateCatalog();
        }
    }

    // API 21–32 lack the public not-exported flag; a signature permission limits senders instead.
    @SuppressLint("UnspecifiedRegisterReceiverFlag")
    private void registerLegacyStateReceiver(IntentFilter filter) {
        registerReceiver(stateReceiver, filter, "org.restunts.genghost.permission.STATE", null);
    }

    @Override
    protected void onPause() {
        saveOptions();
        super.onPause();
    }

    @Override
    protected void onStop() {
        MAIN_THREAD.removeCallbacks(ticker);
        if (receiverRegistered) {
            unregisterReceiver(stateReceiver);
            receiverRegistered = false;
        }
        if (active.get() == this) {
            active.clear();
        }
        super.onStop();
    }

    @Override
    protected void onSaveInstanceState(Bundle state) {
        saveOptions();
        state.putString("export_source", exportSource);
        super.onSaveInstanceState(state);
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private LinearLayout.LayoutParams fullWidth() {
        return new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
    }

    private TextView text(LinearLayout parent, String value, int size) {
        TextView result = new TextView(this);
        result.setText(value);
        result.setTextSize(size);
        result.setTextColor(Color.rgb(219, 228, 235));
        result.setPadding(0, dp(5), 0, dp(5));
        parent.addView(result, fullWidth());
        return result;
    }

    private TextView label(LinearLayout parent, String value) {
        TextView result = text(parent, value, 16);
        result.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        result.setPadding(0, dp(21), 0, dp(3));
        return result;
    }

    private LinearLayout row(LinearLayout parent) {
        LinearLayout result = new LinearLayout(this);
        result.setOrientation(LinearLayout.HORIZONTAL);
        parent.addView(result, fullWidth());
        return result;
    }

    private Button button(LinearLayout parent, String value, View.OnClickListener listener,
            boolean weighted) {
        Button result = new Button(this);
        result.setText(value);
        result.setAllCaps(false);
        result.setOnClickListener(listener);
        parent.addView(result, weighted
            ? new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1)
            : fullWidth());
        return result;
    }

    private Spinner spinner(LinearLayout parent, List<String> values) {
        Spinner result = new Spinner(this);
        result.setMinimumHeight(dp(48));
        setItems(result, values);
        parent.addView(result, fullWidth());
        return result;
    }

    private void setItems(Spinner target, List<String> values) {
        ArrayAdapter<String> adapter = new ArrayAdapter<>(this,
            android.R.layout.simple_spinner_item, values);
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        target.setAdapter(adapter);
    }

    private CheckBox checkbox(LinearLayout parent, String value, String key, boolean fallback) {
        CheckBox result = new CheckBox(this);
        result.setText(value);
        result.setTextSize(15);
        result.setChecked(preferences.getBoolean(key, fallback));
        parent.addView(result, fullWidth());
        return result;
    }

    private String selected(Spinner spinner) {
        Object value = spinner.getSelectedItem();
        return value == null ? "" : value.toString();
    }

    private void saveOptions() {
        SharedPreferences.Editor changes = preferences.edit()
            .putString("minutes", minutes.getText().toString())
            .putString("cores", cores.getText().toString())
            .putString("strategy", STRATEGIES[strategy.getSelectedItemPosition()])
            .putString("mode", MODES[mode.getSelectedItemPosition()])
            .remove("fine_tune")
            .remove("transmission")
            .putBoolean("power_gear", powerGear.isChecked())
            .putBoolean("collision", collision.isChecked())
            .putBoolean("left_bias", leftBias.isChecked())
            .putBoolean("dual_way_switching", dualWaySwitching.isChecked())
            .putBoolean("owoot", owoot.isChecked())
            .putBoolean("amiga_grass", amigaGrass.isChecked());
        if (track.getSelectedItem() != null) {
            changes.putString("track", selected(track));
        }
        if (car.getSelectedItem() != null) {
            changes.putString("car", selected(car));
        }
        changes.apply();
    }

    private void populateCatalog() {
        String previousTrack = selected(track);
        String previousCar = selected(car);
        setItems(track, availableTracks);
        setItems(car, availableCars);
        String rememberedTrack = previousTrack.isEmpty()
            ? preferences.getString("track", "") : previousTrack;
        String rememberedCar = previousCar.isEmpty()
            ? preferences.getString("car", "COUN") : previousCar;
        track.setSelection(Math.max(0, availableTracks.indexOf(rememberedTrack)));
        car.setSelection(Math.max(0, availableCars.indexOf(rememberedCar)));
        refreshState();
    }

    private void refreshState() {
        generation = GeneratorService.loadState(this);
        boolean busy = generation.running || folderBusy;
        folderStatus.setText(folderMessage);
        chooseFolder.setEnabled(!busy);
        refreshFolder.setEnabled(!busy && GameDataStorage.folder(this) != null);
        track.setEnabled(!busy && !availableTracks.isEmpty());
        mode.setEnabled(!busy);
        carLabel.setVisibility(fineTuningSelected() ? View.GONE : View.VISIBLE);
        car.setVisibility(fineTuningSelected() ? View.GONE : View.VISIBLE);
        car.setEnabled(!busy && !fineTuningSelected() && !availableCars.isEmpty());
        minutes.setEnabled(!busy);
        cores.setEnabled(!busy);
        strategy.setEnabled(!busy);
        powerGear.setEnabled(!busy);
        collision.setEnabled(!busy);
        leftBias.setEnabled(!busy);
        dualWaySwitching.setEnabled(!busy);
        owoot.setEnabled(!busy);
        amigaGrass.setEnabled(!busy);
        generate.setEnabled(!busy && catalogReady && !availableTracks.isEmpty()
            && (fineTuningSelected() || !availableCars.isEmpty()));
        cancel.setEnabled(generation.running);
        export.setEnabled(!generation.running && !exporting && generation.outputAvailable);
        exportReport.setEnabled(!generation.running && !exporting && generation.reportAvailable);
        String message = generation.message;
        if (message == null || message.isEmpty()) {
            message = "Ready to generate a replay.";
        }
        if (generation.running) {
            long elapsed = Math.max(0, (System.currentTimeMillis() - generation.startTimeMillis) / 1000);
            message += String.format(Locale.ROOT, "\nElapsed %d:%02d", elapsed / 60, elapsed % 60);
            message += "\nCPU cores: " + (generation.cores == 0
                ? "all available" : Integer.toString(generation.cores));
            message += "\n" + STRATEGY_LABELS["state".equals(generation.strategy) ? 1 : 0];
            if (generation.fineTune) {
                message += generation.fineTuneGearsOnly
                    ? "\nFine-tune gears only" : "\nFine-tune replay";
            }
        }
        runStatus.setText(message);
        String currentLog = generation.log == null ? "" : generation.log;
        if (!log.getText().toString().equals(currentLog)) {
            log.setText(currentLog);
        }
    }

    private void chooseGameFolder() {
        Intent picker = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        picker.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION
            | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION | Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
        try {
            startActivityForResult(picker, REQUEST_FOLDER);
        } catch (ActivityNotFoundException error) {
            showError("This device has no folder picker. Enable the Android Files app and try again.");
        }
    }

    private static void collectTracks(File root, File directory, List<String> result, int depth)
            throws IOException {
        if (depth > 16) {
            return;
        }
        File[] files = directory.listFiles();
        if (files == null) {
            throw new IOException("Cannot read the selected game folder.");
        }
        String base = root.getCanonicalPath() + File.separator;
        for (File file : files) {
            if (!file.getCanonicalPath().startsWith(base)) {
                throw new IOException("A game file is outside the selected folder.");
            }
            if (file.isDirectory()) {
                collectTracks(root, file, result, depth + 1);
            } else {
                String name = file.getName().toLowerCase(Locale.ROOT);
                if (name.endsWith(".trk") || name.endsWith(".rpl")) {
                    result.add(file.getCanonicalPath().substring(base.length()));
                }
            }
        }
    }

    private void updateFolder(Uri selectedFolder) {
        if (folderBusy) {
            return;
        }
        saveOptions();
        Context context = getApplicationContext();
        boolean readCacheOnly = GeneratorService.loadState(context).running;
        if (readCacheOnly && selectedFolder != null) {
            return;
        }
        folderBusy = true;
        folderMessage = "Reading your tracks and cars…";
        refreshState();
        FILE_WORKER.execute(() -> {
            List<String> tracks = new ArrayList<>();
            Set<String> cars = Collections.emptySet();
            String message;
            boolean success = false;
            try {
                GeneratorService.prepareCars(context);
                if (!readCacheOnly) {
                    GeneratorService.recoverInterrupted(context);
                }
                if (selectedFolder != null) {
                    GameDataStorage.select(context, selectedFolder);
                } else if (!readCacheOnly) {
                    GameDataStorage.refresh(context);
                }
                File game = new File(context.getFilesDir(), "game");
                collectTracks(game, game, tracks, 0);
                Collections.sort(tracks, String.CASE_INSENSITIVE_ORDER);
                cars = CarResources.available(game, context.getFilesDir());
                message = tracks.size() + " tracks / replays · " + cars.size() + " cars";
                if (tracks.isEmpty()) {
                    message += "\nAdd .TRK or .RPL files to your game folder, then tap Refresh.";
                }
                if (cars.isEmpty()) {
                    message += "\nNo car resources found. Select the folder with your Stunts game data.";
                }
                success = true;
            } catch (IOException | RuntimeException error) {
                message = "Cannot read game folder: " + error.getMessage();
            }
            final String resultMessage = message;
            final boolean resultSuccess = success;
            final List<String> resultCars = new ArrayList<>(cars);
            MAIN_THREAD.post(() -> {
                folderBusy = false;
                catalogReady = resultSuccess;
                folderMessage = resultMessage;
                availableTracks = resultSuccess ? tracks : Collections.emptyList();
                availableCars = resultSuccess ? resultCars : Collections.emptyList();
                GenghostActivity activity = active.get();
                if (activity != null) {
                    activity.populateCatalog();
                }
            });
        });
    }

    private boolean fineTuningSelected() {
        return mode.getSelectedItemPosition() > 0;
    }

    private void startGeneration() {
        if (fineTuningSelected()
                && !selected(track).toLowerCase(Locale.ROOT).endsWith(".rpl")) {
            showError("Fine-tuning requires a completed .RPL replay.");
            return;
        }
        int budget;
        int searchCores;
        try {
            budget = Integer.parseInt(minutes.getText().toString().trim());
            if (budget < 1 || budget > 1440) {
                throw new NumberFormatException();
            }
        } catch (NumberFormatException error) {
            minutes.setError("Enter between 1 and 1440 minutes.");
            minutes.requestFocus();
            return;
        }
        try {
            searchCores = Integer.parseInt(cores.getText().toString().trim());
            if (searchCores < 0 || searchCores > GenerationRequest.MAX_CORES) {
                throw new NumberFormatException();
            }
        } catch (NumberFormatException error) {
            cores.setError("Enter 1 to " + GenerationRequest.MAX_CORES
                + " cores, or 0 for all available cores.");
            cores.requestFocus();
            return;
        }
        List<String> flags = new ArrayList<>();
        flags.add(powerGear.isChecked() ? "--pg:on" : "--pg:off");
        flags.add(collision.isChecked() ? "--lc:on" : "--lc:off");
        flags.add(leftBias.isChecked() ? "--lcb:on" : "--lcb:off");
        flags.add(dualWaySwitching.isChecked() ? "--dws:on" : "--dws:off");
        if (owoot.isChecked()) {
            flags.add("--owoot");
        }
        if (amigaGrass.isChecked()) {
            flags.add("/amigagrass");
        }
        saveOptions();
        if (Build.VERSION.SDK_INT >= 33
                && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)
                    != PackageManager.PERMISSION_GRANTED
                && !preferences.getBoolean("notification_requested", false)) {
            preferences.edit().putBoolean("notification_requested", true).apply();
            requestPermissions(new String[] {Manifest.permission.POST_NOTIFICATIONS},
                REQUEST_NOTIFICATIONS);
        }
        try {
            GeneratorService.start(this, new GenerationRequest(selected(track),
                fineTuningSelected() ? "" : selected(car), budget * 60, searchCores,
                STRATEGIES[strategy.getSelectedItemPosition()],
                MODES[mode.getSelectedItemPosition()], flags.toArray(new String[0])));
            refreshState();
        } catch (RuntimeException error) {
            showError("Cannot start the search: " + error.getMessage());
        }
    }

    private void chooseExport(boolean report) {
        refreshState();
        exportSource = report ? generation.reportName : generation.outputName;
        if (exportSource == null || exportSource.isEmpty()) {
            return;
        }
        Intent picker = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        picker.addCategory(Intent.CATEGORY_OPENABLE);
        picker.setType(report ? "application/json" : "application/octet-stream");
        picker.putExtra(Intent.EXTRA_TITLE, exportSource);
        try {
            startActivityForResult(picker, REQUEST_EXPORT);
        } catch (ActivityNotFoundException error) {
            showError("This device has no file picker. Enable the Android Files app and try again.");
        }
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (result != RESULT_OK || data == null || data.getData() == null) {
            return;
        }
        Uri selectedUri = data.getData();
        if (request == REQUEST_FOLDER) {
            try {
                int required = Intent.FLAG_GRANT_READ_URI_PERMISSION
                    | Intent.FLAG_GRANT_WRITE_URI_PERMISSION;
                if ((data.getFlags() & required) != required) {
                    throw new SecurityException("Choose a folder with both read and write access.");
                }
                getContentResolver().takePersistableUriPermission(selectedUri,
                    Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
                updateFolder(selectedUri);
            } catch (RuntimeException error) {
                showError("Cannot keep access to this folder: " + error.getMessage());
            }
        } else if (request == REQUEST_EXPORT && exportSource != null) {
            exportFile(selectedUri, exportSource);
        }
    }

    private void exportFile(Uri destination, String name) {
        exporting = true;
        refreshState();
        Context context = getApplicationContext();
        FILE_WORKER.execute(() -> {
            String message;
            try {
                File source = GeneratorService.outputFile(context, name);
                try (FileInputStream input = new FileInputStream(source);
                        OutputStream output = context.getContentResolver()
                            .openOutputStream(destination, "wt")) {
                    if (output == null) {
                        throw new IOException("Cannot write the selected document.");
                    }
                    byte[] buffer = new byte[8192];
                    int count;
                    while ((count = input.read(buffer)) != -1) {
                        output.write(buffer, 0, count);
                    }
                }
                message = "Exported " + name;
            } catch (IOException | RuntimeException error) {
                message = "Could not export: " + error.getMessage();
            }
            final String result = message;
            MAIN_THREAD.post(() -> {
                exporting = false;
                GenghostActivity activity = active.get();
                if (activity != null) {
                    activity.refreshState();
                    new AlertDialog.Builder(activity).setMessage(result)
                        .setPositiveButton("OK", null).show();
                }
            });
        });
    }

    private void showError(String message) {
        new AlertDialog.Builder(this).setTitle("Genghost").setMessage(message)
            .setPositiveButton("OK", null).show();
    }
}
