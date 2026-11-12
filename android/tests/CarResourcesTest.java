package org.restunts.genghost;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.TreeSet;
import java.util.zip.CRC32;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;
import org.restunts.android.GameDataImport;

public final class CarResourcesTest {
    private static final byte[] CONTENT = {1, 2, 3};
    private static final int SUPPORTED_RESOURCE_COUNT = 9;
    private static final int ZIP_LOCAL_HEADER_SIZE = 30;

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

    private static byte[] archiveBytes(boolean stored, String... members) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        try (ZipOutputStream archive = new ZipOutputStream(bytes)) {
            for (String member : members) {
                ZipEntry entry = new ZipEntry(member);
                if (stored) {
                    CRC32 crc = new CRC32();
                    crc.update(CONTENT);
                    entry.setMethod(ZipEntry.STORED);
                    entry.setSize(CONTENT.length);
                    entry.setCrc(crc.getValue());
                }
                archive.putNextEntry(entry);
                archive.write(CONTENT);
                archive.closeEntry();
            }
        }
        return bytes.toByteArray();
    }

    private static void archive(File parent, String name, String member, boolean stored) throws IOException {
        File destination = new File(parent, name);
        GameDataImport.makeDirectory(destination.getParentFile());
        Files.write(destination.toPath(), archiveBytes(stored, member));
    }

    public static void main(String[] args) throws Exception {
        File root = Files.createTempDirectory("restunts-car-resources-test").toFile();
        try {
            File game = new File(root, "game");
            File assets = new File(root, "assets");
            GameDataImport.makeDirectory(game);
            List<String> copied = new ArrayList<>();
            byte[] bundledArchive = archiveBytes(false, "CaRbNdL.ReS");
            CarResources.prepare(assets, new String[] {"CaRTerR.ReS", "STMCMU.3SH", "StMcMu.H3d", "STDAMCMU.VSH",
                "CARMCMU.RES", "CARPACK.PRE", "STPACK.P3S", "STDBPACK.PVS", "BnDl.ZiP", "README.TXT",
                "../OUTSIDE.RES", "nested/CARNEST.RES"}, name -> {
                    copied.add(name);
                    return new ByteArrayInputStream(name.equals("BnDl.ZiP") ? bundledArchive : CONTENT);
                });
            require(copied.size() == SUPPORTED_RESOURCE_COUNT, "Only supported immediate car resources may be extracted");
            require(Arrays.equals(Files.readAllBytes(new File(assets, "cars/carterr.res").toPath()), CONTENT),
                "Bundled resources must retain their bytes under normalized names");
            require(Arrays.equals(Files.readAllBytes(new File(assets, "cars/stmcmu.h3d").toPath()), CONTENT),
                "Optional HyperVision meshes must be extracted with their classic car resources");
            require(Arrays.equals(Files.readAllBytes(new File(assets, "cars/bndl.zip").toPath()), bundledArchive),
                "Bundled car archives must remain a single file with their original bytes");
            require(!new File(root, "OUTSIDE.RES").exists(), "Car extraction must stay in its folder");
            file(game, "CarCoun.res");
            file(game, "CARMCMU.RES");
            file(game, "cars/CarDIA3.Pre");
            file(game, "cars/CAR_CST.RES");
            file(game, "cars/CARMCMU.RES");
            file(game, "cars/nested/CARNEST.RES");
            file(game, "cars/NOTACAR.RES");
            GameDataImport.makeDirectory(new File(game, "CARFAKE.RES"));
            archive(game, "ZiPc.ZiP", "CaRzIpC.ReS", false);
            archive(game, "cars/SuBz.ZIP", "CARSUBZ.PRE", true);
            archive(game, "cars/MCMU.ZIP", "CARMCMU.RES", false);
            archive(game, "BAD1.ZIP", "CARNOPE.RES", false);
            archive(game, "BAD2.ZIP", "nested/CARBAD2.RES", false);
            archive(game, "BAD3.ZIP", "CARBAD3.RES/", false);
            file(game, "BAD4.ZIP");
            archive(game, "TOOLONG.ZIP", "CARTOOLONG.RES", false);
            GameDataImport.makeDirectory(new File(game, "FAKE.ZIP"));
            String corruptMember = "CARBAD5.RES";
            byte[] corrupt = archiveBytes(true, corruptMember);
            corrupt[ZIP_LOCAL_HEADER_SIZE + corruptMember.length()] ^= 1;
            Files.write(new File(game, "BAD5.ZIP").toPath(), corrupt);
            Files.write(new File(game, "DUPR.ZIP").toPath(),
                archiveBytes(false, "CARDUPR.RES", "cardupr.res"));
            Files.write(new File(game, "DUPP.ZIP").toPath(),
                archiveBytes(false, "CARDUPP.PRE", "cardupp.pre"));
            Files.write(new File(game, "BOTH.ZIP").toPath(),
                archiveBytes(false, "CARBOTH.RES", "carboth.res", "CARBOTH.PRE"));
            require(CarResources.available(game, assets).equals(new TreeSet<>(
                Arrays.asList("BNDL", "BOTH", "COUN", "DIA3", "MCMU", "PACK", "SUBZ", "TERR", "ZIPC", "_CST"))),
                "The catalog must combine loose and archived cars in every root, excluding broken or mismatched ZIPs");
            require(!new File(game, "CARZIPC.RES").exists(), "Car discovery must not extract ZIP members");
            require(new File(game, "ZiPc.ZiP").delete(), "Removing a ZIP car must require only one file operation");
            require(!CarResources.available(game, assets).contains("ZIPC"), "Removed archives must leave the catalog");
            try {
                CarResources.available(new File(root, "missing"), assets);
                throw new AssertionError("A missing game directory must fail");
            } catch (IOException expected) {
                // The bundled cars do not replace the required original game folder.
            }
        } finally {
            GameDataImport.delete(root);
        }
        System.out.println("PASS: bundled car resources and loose/ZIP/custom/bundled car catalog");
    }
}
