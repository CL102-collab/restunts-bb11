package org.restunts.android;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

/** Uses the same Munt content recognition as native playback. No ROM data is bundled. */
final class Mt32RomLibrary {
    private static boolean loaded;

    static final class RomSet {
        final String id;
        final String label;

        RomSet(String id, String label) {
            this.id = id;
            this.label = label;
        }

        @Override
        public String toString() {
            return label;
        }
    }

    static final class Inventory {
        final List<RomSet> sets;
        final String error;

        Inventory(List<RomSet> sets, String error) {
            this.sets = sets;
            this.error = error;
        }

        String selectedModel(String remembered) {
            for (RomSet set : sets) {
                if (set.id.equals(remembered)) {
                    return remembered;
                }
            }
            return sets.size() == 1 ? sets.get(0).id : "";
        }

        boolean contains(String model) {
            for (RomSet set : sets) {
                if (set.id.equals(model)) {
                    return true;
                }
            }
            return false;
        }
    }

    private Mt32RomLibrary() {}

    static synchronized String[] libraries() {
        return new String[] {"SDL3", "nuked-opl2", "mt32emu", "main"};
    }

    private static synchronized void load() throws IOException {
        if (!loaded) {
            try {
                for (String library : libraries()) {
                    System.loadLibrary(library);
                }
                loaded = true;
            } catch (LinkageError error) {
                throw new IOException("The MT-32 audio library could not be loaded.", error);
            }
        }
    }

    static Inventory scan(String directory) throws IOException {
        load();
        try {
            JSONObject result = new JSONObject(inventoryNative(directory));
            JSONArray found = result.getJSONArray("sets");
            List<RomSet> sets = new ArrayList<>();
            for (int index = 0; index < found.length(); index++) {
                JSONObject set = found.getJSONObject(index);
                String id = set.getString("id");
                String label = set.getString("label");
                if (id.isEmpty() || label.isEmpty()) {
                    throw new JSONException("Empty ROM set name");
                }
                sets.add(new RomSet(id, label));
            }
            return new Inventory(sets, result.optString("error", ""));
        } catch (JSONException | LinkageError error) {
            throw new IOException("Cannot identify the MT-32 ROM sets.", error);
        }
    }

    private static native String inventoryNative(String directory);
}
