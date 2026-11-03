package org.restunts.android;

import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/** Actual native renderer-compatible devices; no SDL window or game startup is needed. */
public final class VulkanDevices {
    public static final int MINIMUM_VULKAN_API = 24;
    private static final int REPORT_FIELDS_PER_DEVICE = 2;
    private static final int DISPLAY_INDEX_OFFSET = 1;
    private static boolean librariesLoaded;
    private static boolean loadingFailed;

    private VulkanDevices() {
    }

    public static final class Device {
        public final int index;
        public final String name;

        private Device(int index, String name) {
            this.index = index;
            this.name = name;
        }

        @Override
        public String toString() {
            return name + " (" + (index + DISPLAY_INDEX_OFFSET) + ")";
        }
    }

    private static synchronized boolean loadLibraries() {
        if (librariesLoaded || loadingFailed) {
            return librariesLoaded;
        }
        try {
            // Same classloader and dependency order as GameActivity. Loading
            // registers JNI only; SDLActivity initializes its video context later.
            System.loadLibrary("SDL3");
            System.loadLibrary("nuked-opl2");
            System.loadLibrary("main");
            librariesLoaded = true;
        } catch (LinkageError | RuntimeException unavailable) {
            loadingFailed = true;
        }
        return librariesLoaded;
    }

    private static native String[] queryNativeDevices();

    public static List<Device> query(int sdkVersion) {
        if (sdkVersion < MINIMUM_VULKAN_API || !loadLibraries()) {
            return Collections.emptyList();
        }
        try {
            return fromNativeReport(queryNativeDevices());
        } catch (LinkageError | RuntimeException unavailable) {
            return Collections.emptyList();
        }
    }

    static List<Device> fromNativeReport(String[] report) {
        if (report == null || report.length % REPORT_FIELDS_PER_DEVICE != 0) {
            return Collections.emptyList();
        }
        List<Device> devices = new ArrayList<>();
        Set<Integer> indices = new HashSet<>();
        try {
            for (int field = 0; field < report.length; field += REPORT_FIELDS_PER_DEVICE) {
                int index = Integer.parseInt(report[field]);
                String name = report[field + 1];
                if (index < 0 || name == null || name.trim().isEmpty() || !indices.add(index)) {
                    return Collections.emptyList();
                }
                devices.add(new Device(index, name.trim().replace('\n', ' ').replace('\r', ' ')));
            }
        } catch (NumberFormatException invalid) {
            return Collections.emptyList();
        }
        return Collections.unmodifiableList(devices);
    }

    public static Device find(List<Device> devices, int index) {
        for (Device device : devices) {
            if (device.index == index) {
                return device;
            }
        }
        return null;
    }
}
