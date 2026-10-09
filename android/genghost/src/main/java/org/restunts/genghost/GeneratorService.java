package org.restunts.genghost;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.PowerManager;
import android.os.SystemClock;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.UUID;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import org.json.JSONException;
import org.json.JSONObject;
import org.restunts.android.GameDataStorage;

/** Runs the real CLI without SDL's Android activity, a renderer, or JNI engine state. */
public final class GeneratorService extends Service {
    public static final String ACTION_STATE = "org.restunts.genghost.STATE";
    private static final String ACTION_START = "org.restunts.genghost.START";
    private static final String ACTION_CANCEL = "org.restunts.genghost.CANCEL";
    private static final String CHANNEL = "generation";
    private static final String PREFERENCES = "generation";
    private static final String PID_PREFIX = "GENGHOST_CHILD_PID=";
    private static final int NOTIFICATION_ID = 1;
    private static final int MAX_LOG = 24000;
    private static final long STOP_GRACE_MS = 15000;
    private static final Object RECOVERY_LOCK = new Object();
    private static volatile GeneratorService active;
    private static volatile State pendingStart;
    private static volatile boolean recovering;
    private static boolean pendingCancelled;

    private final Object stateLock = new Object();
    private final Object processLock = new Object();
    private final Handler main = new Handler(Looper.getMainLooper());
    private State state = new State();
    private volatile boolean cancelled;
    private volatile boolean workerRunning;
    private volatile boolean destroyed;
    private long startedElapsed;
    private long cancelledElapsed;
    private Process child;
    private int childPid;
    private boolean terminateSent;
    private boolean forced;
    private long generationSerial;
    private PowerManager.WakeLock wakeLock;

    /** Immutable to callers by convention: loadState always returns an independent snapshot. */
    public static final class State {
        public String phase = "idle";
        public String message = "Choose a game folder, track, and car to begin.";
        public String log = "";
        public String outputName = "";
        public String reportName = "";
        public boolean running;
        public boolean outputAvailable;
        public boolean reportAvailable;
        public int seconds = GenerationRequest.DEFAULT_SECONDS;
        public int cores = GenerationRequest.DEFAULT_CORES;
        public String strategy = GenerationRequest.DEFAULT_STRATEGY;
        public int exitCode = -1;
        public long startTimeMillis;
        public long elapsedSeconds;
        public int nativePid;
        public String nativeToken = "";
        public boolean recoveryAttempted;

        State() {}

        State(JSONObject saved) {
            phase = saved.optString("phase", phase);
            message = saved.optString("message", message);
            log = saved.optString("log", "");
            outputName = saved.optString("outputName", "");
            reportName = saved.optString("reportName", "");
            running = saved.optBoolean("running");
            outputAvailable = saved.optBoolean("outputAvailable");
            reportAvailable = saved.optBoolean("reportAvailable");
            seconds = saved.optInt("seconds", seconds);
            cores = saved.optInt("cores", cores);
            strategy = saved.optString("strategy", strategy);
            exitCode = saved.optInt("exitCode", -1);
            startTimeMillis = saved.optLong("startTimeMillis");
            elapsedSeconds = saved.optLong("elapsedSeconds");
            nativePid = saved.optInt("nativePid");
            nativeToken = saved.optString("nativeToken", "");
            recoveryAttempted = saved.optBoolean("recoveryAttempted");
        }

        JSONObject json() {
            JSONObject result = new JSONObject();
            try {
                result.put("phase", phase).put("message", message).put("log", log)
                    .put("outputName", outputName).put("reportName", reportName)
                    .put("running", running).put("outputAvailable", outputAvailable)
                    .put("reportAvailable", reportAvailable).put("seconds", seconds)
                    .put("cores", cores).put("strategy", strategy)
                    .put("exitCode", exitCode).put("startTimeMillis", startTimeMillis)
                    .put("elapsedSeconds", elapsedSeconds).put("nativePid", nativePid)
                    .put("nativeToken", nativeToken).put("recoveryAttempted", recoveryAttempted);
            } catch (JSONException impossible) {
                throw new IllegalStateException(impossible);
            }
            return result;
        }
    }

    public static synchronized void start(Context context, GenerationRequest request) {
        if (isRunning()) {
            throw new IllegalStateException("A search is already running.");
        }
        State previous = loadState(context);
        State preparing = new State();
        preparing.phase = "preparing";
        preparing.message = "Starting generation…";
        preparing.running = true;
        preparing.seconds = request.seconds;
        preparing.cores = request.cores;
        preparing.strategy = request.strategy;
        preparing.startTimeMillis = System.currentTimeMillis();
        pendingStart = preparing;
        pendingCancelled = false;
        context.getSharedPreferences(PREFERENCES, MODE_PRIVATE).edit()
            .putString("state", preparing.json().toString()).apply();
        Intent intent = new Intent(context, GeneratorService.class).setAction(ACTION_START);
        request.putInto(intent);
        try {
            if (Build.VERSION.SDK_INT >= 26) {
                context.startForegroundService(intent);
            } else {
                context.startService(intent);
            }
        } catch (RuntimeException error) {
            pendingStart = null;
            context.getSharedPreferences(PREFERENCES, MODE_PRIVATE).edit()
                .putString("state", previous.json().toString()).apply();
            throw error;
        }
        context.sendBroadcast(new Intent(ACTION_STATE).setPackage(context.getPackageName()));
    }

    public static synchronized void cancel(Context context) {
        if (pendingStart != null) {
            pendingCancelled = true;
            State stopping = new State(pendingStart.json());
            stopping.phase = "stopping";
            stopping.message = "Cancelling generation…";
            pendingStart = stopping;
        }
        GeneratorService service = active;
        if (service != null) {
            service.requestStop("Stopping; preserving the best completed replay…");
        }
    }

    public static boolean isRunning() {
        GeneratorService service = active;
        return recovering || pendingStart != null || (service != null && service.workerRunning);
    }

    public static State loadState(Context context) {
        State pending = pendingStart;
        if (pending != null) {
            return new State(pending.json());
        }
        GeneratorService service = active;
        if (service != null) {
            return service.snapshot();
        }
        State saved = readSavedState(context);
        if (saved.running) {
            saved.running = false;
            saved.phase = "interrupted";
            saved.message = "Android stopped the previous search. Checking for a saved replay…";
            saved.outputAvailable = false;
            saved.reportAvailable = false;
        }
        try {
            saved.outputAvailable &= outputFile(context, saved.outputName).isFile();
            saved.reportAvailable &= outputFile(context, saved.reportName).isFile();
        } catch (IOException invalid) {
            saved.outputAvailable = false;
            saved.reportAvailable = false;
        }
        return saved;
    }

    private static State readSavedState(Context context) {
        try {
            return new State(new JSONObject(context.getSharedPreferences(PREFERENCES,
                MODE_PRIVATE).getString("state", "{}")));
        } catch (JSONException malformed) {
            return new State();
        }
    }

    /** Resolve exports independently of the replaceable game cache. */
    public static File outputFile(Context context, String name) throws IOException {
        if (name == null || !name.matches(
                "(g[0-9a-f]{7}|ghost-[a-z0-9]{4}-[0-9a-f]{12})\\.rpl(\\.json)?")) {
            throw new IOException("Invalid generated replay filename.");
        }
        File retainedRoot = new File(context.getFilesDir(), "ghost-results").getCanonicalFile();
        File retained = new File(retainedRoot, name).getCanonicalFile();
        if (!retainedRoot.equals(retained.getParentFile())) {
            throw new IOException("The generated replay is outside its result folder.");
        }
        if (retained.isFile()) {
            return retained;
        }
        File game = new File(context.getFilesDir(), "game").getCanonicalFile();
        File source = new File(game, name).getCanonicalFile();
        if (!game.equals(source.getParentFile())) {
            throw new IOException("The generated replay is outside the game folder.");
        }
        return source;
    }

    private static void retain(Context context, File source, String directory) throws IOException {
        if (source.length() > 65536) {
            throw new IOException("The generated output is too large.");
        }
        File root = new File(context.getFilesDir(), directory);
        if (!root.isDirectory() && !root.mkdirs()) {
            throw new IOException("Cannot retain the generated replay on this device.");
        }
        File destination = new File(root, source.getName());
        File temporary = new File(root, source.getName() + "." + UUID.randomUUID() + ".tmp");
        try {
            try (FileInputStream input = new FileInputStream(source);
                    FileOutputStream output = new FileOutputStream(temporary)) {
                byte[] buffer = new byte[8192];
                int count;
                int total = 0;
                while ((count = input.read(buffer)) != -1) {
                    total += count;
                    if (total > 65536) {
                        throw new IOException("The generated output is too large.");
                    }
                    output.write(buffer, 0, count);
                }
                output.getFD().sync();
            }
            if (!temporary.renameTo(destination)) {
                throw new IOException("Cannot preserve the generated replay.");
            }
        } finally {
            temporary.delete();
        }
    }

    private String uniqueOutputName(File game) throws IOException {
        File retained = new File(getFilesDir(), "ghost-results");
        for (int attempt = 0; attempt < 128; attempt++) {
            // The game's replay chooser accepts DOS 8.3 names only.
            String name = "g" + UUID.randomUUID().toString().substring(0, 7) + ".rpl";
            if (!outputExists(game, name) && !outputExists(retained, name)) {
                return name;
            }
        }
        throw new IOException("Cannot choose an unused replay filename.");
    }

    private static boolean outputExists(File directory, String name) throws IOException {
        if (!directory.exists()) {
            return false;
        }
        String[] files = directory.list();
        if (files == null) {
            throw new IOException("Cannot read the generated replay folder.");
        }
        for (String existing : files) {
            if (existing.equalsIgnoreCase(name) || existing.equalsIgnoreCase(name + ".json")) {
                return true;
            }
        }
        return false;
    }

    /** Call on a storage worker before refresh/select can replace the old game's cache. */
    public static void recoverInterrupted(Context context) throws IOException {
        synchronized (RECOVERY_LOCK) {
            if (isRunning()) {
                return;
            }
            recovering = true;
            try {
                State saved = readSavedState(context);
                if ((saved.outputAvailable && !saved.running)
                        || saved.outputName.isEmpty() || saved.recoveryAttempted) {
                    return;
                }
                stopOrphan(saved);
                File retainedReplay = outputFile(context, saved.outputName);
                File retainedReport = outputFile(context, saved.reportName);
                File game = new File(context.getFilesDir(), "game").getCanonicalFile();
                File replay = new File(game, saved.outputName).getCanonicalFile();
                File report = new File(game, saved.reportName).getCanonicalFile();
                if (!game.equals(replay.getParentFile()) || !game.equals(report.getParentFile())) {
                    throw new IOException("Interrupted output is outside the game folder.");
                }
                if (!replay.isFile() && retainedReplay.isFile()) {
                    retain(context, retainedReplay, "game");
                }
                if (!report.isFile() && retainedReport.isFile()) {
                    retain(context, retainedReport, "game");
                }
                saved.running = false;
                saved.outputAvailable = false;
                saved.reportAvailable = false;
                saved.nativePid = 0;
                saved.nativeToken = "";
                saved.recoveryAttempted = true;
                // Keep source files even if interruption occurred between the two native renames.
                if (replay.isFile()) {
                    retain(context, replay, "ghost-recovery");
                }
                if (report.isFile()) {
                    retain(context, report, "ghost-recovery");
                }
                if (!replay.isFile() || !report.isFile()) {
                    saved.phase = "interrupted";
                    saved.message = "The previous search stopped before saving a complete replay.";
                    saveRecoveredState(context, saved);
                    return;
                }
                try {
                    JSONObject metadata = readReport(report);
                    String car = metadata.getString("car");
                    validateOutput(replay, report, car);
                    verifyRecovered(context, replay, metadata);
                    retain(context, replay, "ghost-results");
                    retain(context, report, "ghost-results");
                    GameDataStorage.publish(context, replay.getAbsolutePath());
                    String exportError = GameDataStorage.lastError(context);
                    GameDataStorage.publish(context, report.getAbsolutePath());
                    if (exportError.isEmpty()) {
                        exportError = GameDataStorage.lastError(context);
                    }
                    saved.phase = "recovered";
                    saved.outputAvailable = true;
                    saved.reportAvailable = true;
                    saved.message = String.format(Locale.ROOT,
                        "Recovered the best completed replay: %.2f s including penalties.",
                        metadata.getDouble("total_seconds"));
                    if (!exportError.isEmpty()) {
                        saved.message += " " + exportError + " Use Export to save another copy.";
                    }
                } catch (IOException | JSONException invalid) {
                    saved.phase = "interrupted";
                    saved.message = "The interrupted replay could not be verified. "
                        + "Its files were retained on this device; no replay was published.";
                    saved.log += "\nRecovery: " + invalid.getMessage() + "\n";
                    if (saved.log.length() > MAX_LOG) {
                        saved.log = saved.log.substring(saved.log.length() - MAX_LOG);
                    }
                }
                saveRecoveredState(context, saved);
            } finally {
                recovering = false;
            }
        }
    }

    private static void saveRecoveredState(Context context, State saved) throws IOException {
        if (!context.getSharedPreferences(PREFERENCES, MODE_PRIVATE).edit()
                .putString("state", saved.json().toString()).commit()) {
            throw new IOException("Cannot retain the recovered search status.");
        }
        GeneratorService service = active;
        if (service != null && !service.workerRunning) {
            synchronized (service.stateLock) {
                service.state = new State(saved.json());
            }
        }
        context.sendBroadcast(new Intent(ACTION_STATE).setPackage(context.getPackageName()));
    }

    private static String processToken(int pid) {
        if (pid <= 1 || pid == android.os.Process.myPid()) {
            return "";
        }
        try (FileInputStream input = new FileInputStream("/proc/" + pid + "/stat")) {
            byte[] bytes = new byte[4096];
            int count = input.read(bytes);
            if (count <= 0) {
                return "";
            }
            String stat = new String(bytes, 0, count, StandardCharsets.US_ASCII);
            String[] fields = stat.substring(stat.lastIndexOf(')') + 2).trim().split("\\s+");
            // Field 22 (start ticks) protects against signalling a recycled process ID.
            return fields.length > 19 && !fields[0].equals("Z") ? fields[19] : "";
        } catch (IOException | RuntimeException unavailable) {
            return "";
        }
    }

    private static void stopOrphan(State saved) throws IOException {
        if (saved.nativeToken.isEmpty()
                || !saved.nativeToken.equals(processToken(saved.nativePid))) {
            return;
        }
        android.os.Process.sendSignal(saved.nativePid, 15);
        long deadline = SystemClock.elapsedRealtime() + STOP_GRACE_MS;
        while (saved.nativeToken.equals(processToken(saved.nativePid))) {
            if (SystemClock.elapsedRealtime() >= deadline) {
                android.os.Process.killProcess(saved.nativePid);
                return;
            }
            try {
                Thread.sleep(50);
            } catch (InterruptedException interrupted) {
                Thread.currentThread().interrupt();
                throw new IOException("Interrupted while stopping the previous search.", interrupted);
            }
        }
    }

    private static void verifyRecovered(Context context, File replay, JSONObject report)
            throws IOException, JSONException {
        String car = report.getString("car");
        String transmission = report.getString("transmission");
        if (!car.matches("[A-Za-z0-9]{4}")
                || !(transmission.equals("manual") || transmission.equals("auto"))) {
            throw new IOException("The interrupted report has invalid car settings.");
        }
        JSONObject physics = report.getJSONObject("physics");
        List<String> command = new ArrayList<>();
        command.add(new File(context.getApplicationInfo().nativeLibraryDir,
            "libgenghost_exec.so").getAbsolutePath());
        command.add("--data-dir");
        File game = new File(context.getFilesDir(), "game");
        command.add(game.getAbsolutePath());
        command.add(replay.getAbsolutePath());
        command.add(car);
        command.add("--verify");
        command.add("--transmission");
        command.add(transmission);
        for (String key : new String[] {"pg", "lc", "lcb"}) {
            command.add("--" + key + ":" + (physics.getBoolean(key) ? "on" : "off"));
        }
        if (physics.getBoolean("owoot")) {
            command.add("--owoot");
        }
        Process process = new ProcessBuilder(command).directory(game)
            .redirectErrorStream(true).start();
        StringBuilder output = new StringBuilder();
        AtomicReference<IOException> readFailure = new AtomicReference<>();
        Thread reader = new Thread(() -> {
            try (InputStreamReader input = new InputStreamReader(process.getInputStream(),
                    StandardCharsets.UTF_8)) {
                char[] buffer = new char[1024];
                int count;
                while ((count = input.read(buffer)) != -1) {
                    synchronized (output) {
                        output.append(buffer, 0, count);
                        if (output.length() > MAX_LOG) {
                            output.delete(0, output.length() - MAX_LOG);
                        }
                    }
                }
            } catch (IOException error) {
                readFailure.set(error);
            }
        }, "genghost-recovery-log");
        reader.setDaemon(true);
        reader.start();
        try {
            long deadline = SystemClock.elapsedRealtime() + 30000;
            while (isAlive(process)) {
                if (SystemClock.elapsedRealtime() >= deadline) {
                    throw new IOException("Interrupted replay verification exceeded 30 seconds.");
                }
                Thread.sleep(50);
            }
            reader.join(1000);
            if (reader.isAlive() || readFailure.get() != null || process.exitValue() != 0) {
                throw new IOException("The interrupted replay did not finish during verification.");
            }
            String text;
            synchronized (output) {
                text = output.toString();
            }
            JSONObject verified = null;
            for (String line : text.split("\n")) {
                if (line.trim().startsWith("{\"finished\":")) {
                    verified = new JSONObject(line.trim());
                }
            }
            if (verified == null || !verified.getBoolean("finished")) {
                throw new IOException("The interrupted replay has no verified finish.");
            }
            for (String key : new String[] {"finish_frame", "penalty_frames", "total_frames"}) {
                if (verified.getInt(key) != report.getInt(key)) {
                    throw new IOException("The interrupted replay and report describe different runs.");
                }
            }
        } catch (InterruptedException interrupted) {
            Thread.currentThread().interrupt();
            throw new IOException("Interrupted while verifying the saved replay.", interrupted);
        } finally {
            if (isAlive(process)) {
                process.destroy();
            }
        }
    }

    private State snapshot() {
        synchronized (stateLock) {
            return new State(state.json());
        }
    }

    @Override
    public void onCreate() {
        super.onCreate();
        state = loadState(this);
        active = this;
        if (Build.VERSION.SDK_INT >= 26) {
            NotificationChannel channel = new NotificationChannel(CHANNEL,
                "Ghost generation", NotificationManager.IMPORTANCE_LOW);
            channel.setDescription("Progress and stop control for offline ghost searches");
            getSystemService(NotificationManager.class).createNotificationChannel(channel);
        }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent == null || ACTION_CANCEL.equals(intent.getAction())) {
            if (workerRunning) {
                requestStop("Stopping; preserving the best completed replay…");
            } else {
                stopSelf();
            }
            return START_NOT_STICKY;
        }
        if (!ACTION_START.equals(intent.getAction()) || workerRunning) {
            return START_NOT_STICKY;
        }
        GenerationRequest request = GenerationRequest.from(intent);
        cancelled = false;
        terminateSent = false;
        forced = false;
        startedElapsed = SystemClock.elapsedRealtime();
        synchronized (stateLock) {
            state = new State();
            state.phase = "preparing";
            state.message = "Refreshing your game folder…";
            state.running = true;
            state.seconds = request.seconds;
            state.cores = request.cores;
            state.strategy = request.strategy;
            state.startTimeMillis = System.currentTimeMillis();
        }
        workerRunning = true;
        boolean stopPending;
        synchronized (GeneratorService.class) {
            stopPending = pendingCancelled;
            pendingCancelled = false;
            pendingStart = null;
        }
        final long serial = ++generationSerial;
        Notification notification = notification(snapshot());
        try {
            if (Build.VERSION.SDK_INT >= 29) {
                startForeground(NOTIFICATION_ID, notification,
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC);
            } else {
                startForeground(NOTIFICATION_ID, notification);
            }
        } catch (RuntimeException unavailable) {
            workerRunning = false;
            synchronized (stateLock) {
                state.running = false;
                state.phase = "failed";
                state.message = "Cannot start background generation: " + unavailable.getMessage();
            }
            publishState(false);
            stopSelf();
            return START_NOT_STICKY;
        }
        if (stopPending) {
            requestStop("Cancelling generation…");
        }
        publishState(false);
        main.postDelayed(heartbeat, 1000);
        Thread worker = new Thread(() -> generate(request, serial), "genghost-search");
        worker.start();
        return START_NOT_STICKY;
    }

    private void generate(GenerationRequest request, long serial) {
        try {
            checkCancelled();
            File game = new File(getFilesDir(), "game").getCanonicalFile();
            // Validate before a provider operation, then recheck after refreshing its files.
            request.validate(game);
            wakeLock = ((PowerManager)getSystemService(POWER_SERVICE)).newWakeLock(
                PowerManager.PARTIAL_WAKE_LOCK, "genghost:search");
            wakeLock.setReferenceCounted(false);
            wakeLock.acquire((request.seconds + 120L) * 1000L);
            refreshData();
            checkCancelled();
            File track = request.validate(game);
            File executable = new File(getApplicationInfo().nativeLibraryDir, "libgenghost_exec.so");
            if (!executable.isFile() || !executable.canExecute()) {
                throw new IOException("The packaged ARMv7 generator is unavailable.");
            }
            String outputName = uniqueOutputName(game);
            File output = new File(game, outputName);
            synchronized (stateLock) {
                state.outputName = outputName;
                state.reportName = outputName + ".json";
            }
            update("running", "Searching and refining routes…");
            int exitCode = runNative(request.arguments(executable, game, track, output), game);
            synchronized (stateLock) {
                state.exitCode = exitCode;
            }
            if (exitCode == 0) {
                File report = new File(game, outputName + ".json");
                JSONObject result = validateOutput(output, report, request.car);
                retain(this, output, "ghost-results");
                retain(this, report, "ghost-results");
                synchronized (stateLock) {
                    state.outputAvailable = true;
                    state.reportAvailable = true;
                }
                update("saving", "Saving the completed replay and physics report…");
                GameDataStorage.publish(this, output.getAbsolutePath());
                String exportError = GameDataStorage.lastError(this);
                GameDataStorage.publish(this, report.getAbsolutePath());
                if (exportError.isEmpty()) {
                    exportError = GameDataStorage.lastError(this);
                }
                String score = String.format(Locale.ROOT, "%.2f s including penalties",
                    result.getDouble("total_seconds"));
                String message = (cancelled ? "Stopped. Best replay saved: " : "Replay saved: ")
                    + score + ".";
                if (!exportError.isEmpty()) {
                    message += " " + exportError + " Use Export to save another copy.";
                }
                update(cancelled ? "cancelled" : "completed", message);
            } else if (cancelled) {
                update("cancelled", forced ? "Stopped. No completed replay was published."
                    : "Stopped before finding a completed replay.");
            } else if (exitCode == 2) {
                update("no_result", "No completed replay found within this budget. "
                    + "Try a longer search or a replay as the starting track.");
            } else {
                update("failed", "Generator exited with code " + exitCode
                    + ". See the search log for details.");
            }
        } catch (Exception error) {
            appendLog("\n" + error.getClass().getSimpleName() + ": " + error.getMessage() + "\n");
            update(cancelled ? "cancelled" : "failed", cancelled ? "Search cancelled."
                : "Cannot generate this ghost: " + error.getMessage());
        } finally {
            synchronized (processLock) {
                if (child != null && isAlive(child)) {
                    if (childPid > 1) {
                        android.os.Process.killProcess(childPid);
                    }
                    child.destroy();
                }
                child = null;
                childPid = 0;
            }
            if (wakeLock != null && wakeLock.isHeld()) {
                wakeLock.release();
            }
            synchronized (stateLock) {
                state.running = false;
                state.elapsedSeconds = (SystemClock.elapsedRealtime() - startedElapsed) / 1000;
            }
            workerRunning = false;
            publishState(false);
            main.post(() -> {
                if (generationSerial != serial || pendingStart != null) {
                    return;
                }
                main.removeCallbacks(heartbeat);
                stopForeground(false);
                publishState(true);
                stopSelf();
            });
        }
    }

    /** Provider reads can stall; cancelling preparation must never launch the engine afterward. */
    private void refreshData() throws Exception {
        CountDownLatch done = new CountDownLatch(1);
        AtomicReference<Exception> failure = new AtomicReference<>();
        Thread refresh = new Thread(() -> {
            try {
                GameDataStorage.refresh(getApplicationContext());
            } catch (Exception error) {
                failure.set(error);
            } finally {
                done.countDown();
            }
        }, "genghost-game-refresh");
        refresh.setDaemon(true);
        refresh.start();
        while (!done.await(250, TimeUnit.MILLISECONDS)) {
            if (cancelled) {
                refresh.interrupt();
                checkCancelled();
            }
        }
        checkCancelled();
        if (failure.get() != null) {
            throw failure.get();
        }
    }

    private int runNative(List<String> arguments, File game) throws Exception {
        List<String> command = new ArrayList<>();
        command.add("/system/bin/sh");
        command.add("-c");
        // The fixed script prints the child's PID and replaces itself. Arguments are never code.
        // Process.destroy() on older Android releases can use SIGKILL, losing the final replay.
        command.add("printf 'GENGHOST_CHILD_PID=%s\\n' \"$$\"; exec \"$@\"");
        command.add("genghost");
        command.addAll(arguments);
        Process process;
        synchronized (processLock) {
            checkCancelled();
            process = new ProcessBuilder(command).directory(game).redirectErrorStream(true).start();
            child = process;
        }
        try (InputStreamReader reader = new InputStreamReader(process.getInputStream(),
                StandardCharsets.UTF_8)) {
            char[] buffer = new char[1024];
            StringBuilder pending = new StringBuilder();
            boolean firstLine = true;
            int count;
            while ((count = reader.read(buffer)) != -1) {
                for (int i = 0; i < count; i++) {
                    if (buffer[i] == '\n' || pending.length() >= 4096) {
                        String line = pending.toString();
                        pending.setLength(0);
                        if (firstLine && line.startsWith(PID_PREFIX)) {
                            int pid = Integer.parseInt(line.substring(PID_PREFIX.length()).trim());
                            if (pid <= 1 || pid == android.os.Process.myPid()) {
                                throw new IOException("Invalid generator process ID.");
                            }
                            synchronized (processLock) {
                                childPid = pid;
                            }
                            synchronized (stateLock) {
                                state.nativePid = pid;
                                state.nativeToken = processToken(pid);
                            }
                            publishState(false);
                            signalCancellation();
                        } else {
                            appendLog(line + "\n");
                        }
                        firstLine = false;
                    } else {
                        pending.append(buffer[i]);
                    }
                }
            }
            if (pending.length() != 0) {
                appendLog(pending.toString());
            }
        }
        int exitCode = process.waitFor();
        synchronized (processLock) {
            child = null;
            childPid = 0;
        }
        synchronized (stateLock) {
            state.nativePid = 0;
            state.nativeToken = "";
        }
        return exitCode;
    }

    private void checkCancelled() throws IOException {
        if (cancelled || destroyed) {
            throw new IOException("Search cancelled.");
        }
    }

    private void requestStop(String message) {
        if (!workerRunning || cancelled) {
            return;
        }
        cancelledElapsed = SystemClock.elapsedRealtime();
        cancelled = true;
        update("stopping", message);
        signalCancellation();
    }

    private static boolean isAlive(Process process) {
        try {
            process.exitValue();
            return false;
        } catch (IllegalThreadStateException running) {
            return true;
        }
    }

    private void signalCancellation() {
        synchronized (processLock) {
            if (!cancelled || child == null || !isAlive(child)) {
                return;
            }
            if (!terminateSent && childPid > 1) {
                android.os.Process.sendSignal(childPid, 15);
                terminateSent = true;
            }
            if (!forced && SystemClock.elapsedRealtime() - cancelledElapsed >= STOP_GRACE_MS) {
                if (childPid > 1) {
                    android.os.Process.killProcess(childPid);
                }
                child.destroy();
                forced = true;
            }
        }
    }

    private final Runnable heartbeat = new Runnable() {
        @Override
        public void run() {
            if (workerRunning) {
                signalCancellation();
                synchronized (stateLock) {
                    state.elapsedSeconds = (SystemClock.elapsedRealtime() - startedElapsed) / 1000;
                }
                publishState(!destroyed);
                main.postDelayed(this, 1000);
            }
        }
    };

    private void appendLog(String text) {
        synchronized (stateLock) {
            String log = state.log + text;
            state.log = log.length() > MAX_LOG ? log.substring(log.length() - MAX_LOG) : log;
        }
    }

    private void update(String phase, String message) {
        synchronized (stateLock) {
            state.phase = phase;
            state.message = message;
        }
        publishState(!destroyed);
    }

    private void publishState(boolean notification) {
        State value = snapshot();
        getSharedPreferences(PREFERENCES, MODE_PRIVATE).edit()
            .putString("state", value.json().toString()).apply();
        sendBroadcast(new Intent(ACTION_STATE).setPackage(getPackageName()));
        if (notification) {
            try {
                ((NotificationManager)getSystemService(NOTIFICATION_SERVICE))
                    .notify(NOTIFICATION_ID, notification(value));
            } catch (SecurityException denied) {
                // Generation remains available if Android 13 notification permission is declined.
            }
        }
    }

    private Notification notification(State value) {
        Notification.Builder builder = Build.VERSION.SDK_INT >= 26
            ? new Notification.Builder(this, CHANNEL) : new Notification.Builder(this);
        int pendingFlags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= 23) {
            pendingFlags |= PendingIntent.FLAG_IMMUTABLE;
        }
        Intent open = getPackageManager().getLaunchIntentForPackage(getPackageName());
        if (open != null) {
            builder.setContentIntent(PendingIntent.getActivity(this, 0, open, pendingFlags));
        }
        builder.setSmallIcon(android.R.drawable.ic_menu_recent_history)
            .setContentTitle(value.running ? "genghost is generating a ghost" : "genghost")
            .setContentText(value.message).setStyle(new Notification.BigTextStyle()
                .bigText(value.message)).setOngoing(value.running).setOnlyAlertOnce(true)
            .setAutoCancel(!value.running).setCategory(Notification.CATEGORY_PROGRESS);
        if (value.running) {
            builder.setProgress(Math.max(1, value.seconds),
                (int)Math.min(value.seconds, value.elapsedSeconds),
                !value.phase.equals("running"));
            Intent stop = new Intent(this, GeneratorService.class).setAction(ACTION_CANCEL);
            builder.addAction(android.R.drawable.ic_menu_close_clear_cancel, "Stop",
                PendingIntent.getService(this, 1, stop, pendingFlags));
        }
        return builder.build();
    }

    private static JSONObject validateOutput(File replay, File report, String car)
            throws IOException, JSONException {
        if (!replay.isFile()) {
            throw new IOException("The generator did not save a complete replay and report.");
        }
        JSONObject result = readReport(report);
        byte[] header = new byte[26];
        try (FileInputStream input = new FileInputStream(replay)) {
            int total = 0;
            while (total < header.length) {
                int count = input.read(header, total, header.length - total);
                if (count < 0) {
                    throw new IOException("The replay header is incomplete.");
                }
                total += count;
            }
        }
        int frames = (header[24] & 255) | ((header[25] & 255) << 8);
        String replayCar = new String(header, 0, 4, StandardCharsets.US_ASCII);
        if (!result.getBoolean("finished") || !result.getString("tool").equals("genghost")
                || !result.getString("car").equalsIgnoreCase(car)
                || !replayCar.equalsIgnoreCase(car) || (header[22] & 255) != 20
                || header[23] != 0 || header[6] != 0 || frames == 0
                || frames != result.getInt("replay_frames") || replay.length() != 1828L + frames
                || result.getInt("finish_frame") <= 0 || result.getInt("finish_frame") > frames
                || result.getInt("total_frames") != ((result.getInt("finish_frame")
                    + result.getInt("penalty_frames")) & 65535)
                || Math.abs(result.getDouble("total_seconds")
                    - result.getInt("total_frames") / 20.0) > 0.001) {
            throw new IOException("The replay and its completion report do not match.");
        }
        return result;
    }

    private static JSONObject readReport(File report) throws IOException, JSONException {
        if (!report.isFile() || report.length() > 65536) {
            throw new IOException("The generator did not save a complete report.");
        }
        ByteArrayOutputStream text = new ByteArrayOutputStream();
        try (FileInputStream input = new FileInputStream(report)) {
            byte[] chunk = new byte[1024];
            int count;
            while ((count = input.read(chunk)) != -1) {
                if (text.size() + count > 65536) {
                    throw new IOException("The generator report is too large.");
                }
                text.write(chunk, 0, count);
            }
        }
        return new JSONObject(new String(text.toByteArray(), StandardCharsets.UTF_8));
    }

    @Override
    public void onTimeout(int startId, int foregroundServiceType) {
        requestStop("Android's background time limit was reached; saving the best replay…");
        // Android grants only a few seconds after this callback. End foreground work promptly.
        main.postDelayed(() -> {
            synchronized (processLock) {
                if (child != null && isAlive(child)) {
                    if (childPid > 1) {
                        android.os.Process.killProcess(childPid);
                    }
                    child.destroy();
                    forced = true;
                }
            }
            stopForeground(true);
            stopSelf();
        }, 3000);
    }

    @Override
    public void onDestroy() {
        destroyed = true;
        requestStop("Android stopped generation; preserving the best completed replay…");
        if (!workerRunning) {
            main.removeCallbacks(heartbeat);
        }
        if (active == this) {
            active = null;
        }
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }
}
