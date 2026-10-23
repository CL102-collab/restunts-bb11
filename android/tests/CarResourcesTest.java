package org.restunts.genghost;

import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.TreeSet;
import org.restunts.android.GameDataImport;

public final class CarResourcesTest {
    private static final byte[] CONTENT = {1, 2, 3};
    private static final int SUPPORTED_RESOURCE_COUNT = 7;

    private static void require(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    private static void file(File parent, String name) throws IOException {
        File destination = new File(parent, name);
        GameDataImport.makeDirectory(destination.getParentFile());
        Files.write(destination.toPath(), CONTENT);
    }

    public static void main(String[] args) throws Exception {
        File root = Files.createTempDirectory("restunts-car-resources-test").toFile();
        try {
            File game = new File(root, "game");
            File assets = new File(root, "assets");
            GameDataImport.makeDirectory(game);
            List<String> copied = new ArrayList<>();
            CarResources.prepare(assets, new String[] {"CaRTerR.ReS", "STMCMU.3SH", "STDAMCMU.VSH",
                "CARMCMU.RES", "CARPACK.PRE", "STPACK.P3S", "STDBPACK.PVS", "README.TXT",
                "../OUTSIDE.RES", "nested/CARNEST.RES"}, name -> {
                    copied.add(name);
                    return new ByteArrayInputStream(CONTENT);
                });
            require(copied.size() == SUPPORTED_RESOURCE_COUNT, "Only supported immediate car resources may be extracted");
            require(Arrays.equals(Files.readAllBytes(new File(assets, "cars/carterr.res").toPath()), CONTENT),
                "Bundled resources must retain their bytes under normalized names");
            require(!new File(root, "OUTSIDE.RES").exists(), "Car extraction must stay in its folder");
            file(game, "CarCoun.res");
            file(game, "CARMCMU.RES");
            file(game, "cars/CarDIA3.Pre");
            file(game, "cars/CAR_CST.RES");
            file(game, "cars/CARMCMU.RES");
            file(game, "cars/nested/CARNEST.RES");
            file(game, "cars/NOTACAR.RES");
            GameDataImport.makeDirectory(new File(game, "CARFAKE.RES"));
            require(CarResources.available(game, assets).equals(new TreeSet<>(
                Arrays.asList("COUN", "DIA3", "MCMU", "PACK", "TERR", "_CST"))),
                "The catalog must combine loose, custom-folder and bundled cars without duplicates");
            try {
                CarResources.available(new File(root, "missing"), assets);
                throw new AssertionError("A missing game directory must fail");
            } catch (IOException expected) {
                // The bundled cars do not replace the required original game folder.
            }
        } finally {
            GameDataImport.delete(root);
        }
        System.out.println("PASS: bundled car extraction and loose/custom/bundled car catalog");
    }
}
