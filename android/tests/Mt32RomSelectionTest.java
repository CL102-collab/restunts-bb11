package org.restunts.android;

import java.util.ArrayList;
import java.util.Arrays;

/** A remembered ROM set must never become an arbitrary choice after a folder change. */
public final class Mt32RomSelectionTest {
    private static void require(boolean condition, String message) {
        if (!condition) {
            throw new AssertionError(message);
        }
    }

    public static void main(String[] args) {
        Mt32RomLibrary.RomSet old = new Mt32RomLibrary.RomSet("mt32_1_07", "MT-32 (old, v1.07)");
        Mt32RomLibrary.RomSet newer = new Mt32RomLibrary.RomSet("mt32_2_07", "MT-32 (new, v2.07)");
        Mt32RomLibrary.Inventory empty = new Mt32RomLibrary.Inventory(new ArrayList<>(),
            "MT-32 control ROM found; compatible PCM ROM missing.");
        require(empty.selectedModel("mt32_1_07").isEmpty(), "Missing PCM must not preserve an unusable set.");
        require(!empty.contains("mt32_1_07"), "Only complete sets can satisfy launch validation.");
        Mt32RomLibrary.Inventory single = new Mt32RomLibrary.Inventory(Arrays.asList(old), "");
        require(single.selectedModel("").equals(old.id), "Automatically choose the only complete set.");
        require(single.selectedModel("unavailable").equals(old.id), "A new sole complete set is unambiguous.");
        Mt32RomLibrary.Inventory several = new Mt32RomLibrary.Inventory(Arrays.asList(old, newer), "");
        require(several.selectedModel("").isEmpty(), "Multiple sets require an explicit first choice.");
        require(several.selectedModel("unavailable").isEmpty(), "Do not silently choose after a ROM set disappears.");
        require(several.selectedModel(newer.id).equals(newer.id), "Keep a remembered available model.");
        require(several.contains(old.id) && several.contains(newer.id), "Recognize both complete choices.");
        require(!several.contains(""), "A placeholder is never a valid launch choice.");
        require(old.toString().equals(old.label), "Show friendly model and firmware labels.");
        System.out.println("Android MT-32 single/multiple/missing and remembered ROM set choices passed.");
    }
}
