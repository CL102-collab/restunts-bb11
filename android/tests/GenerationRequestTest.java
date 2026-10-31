package org.restunts.genghost;

import android.content.Intent;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.util.List;

/** Host checks for mode exclusivity, replay-car selection, and native arguments. */
public final class GenerationRequestTest {
    private interface CheckedAction {
        void run() throws IOException;
    }

    private static void rejected(CheckedAction action) throws IOException {
        try {
            action.run();
        } catch (IOException expected) {
            return;
        }
        throw new AssertionError("Invalid request was accepted");
    }

    private static GenerationRequest request(String source, String car, String mode) {
        return new GenerationRequest(source, car, 60, 2, "state", mode,
            new String[] {"--pg:off", "--owoot"});
    }

    private static byte[] replay() {
        byte[] value = new byte[1829];
        value[0] = 'P';
        value[1] = 'M';
        value[2] = 'I';
        value[3] = 'N';
        value[5] = 1; // Automatic sources are also legal fine-tuning inputs.
        value[22] = 20;
        value[24] = 1;
        return value;
    }

    private static void putWord(byte[] bytes, int offset, int value) {
        bytes[offset] = (byte)value;
        bytes[offset + 1] = (byte)(value >> 8);
    }

    private static void putDword(byte[] bytes, int offset, int value) {
        putWord(bytes, offset, value);
        putWord(bytes, offset + 2, value >> 16);
    }

    private static byte[] extendedTrack() {
        byte[] value = new byte[38];
        byte[] magic = {'C', 'S', 'T', 'R', 'K', 26, '\r', '\n'};
        System.arraycopy(magic, 0, value, 0, magic.length);
        putWord(value, 8, 8);
        putWord(value, 10, 32);
        putWord(value, 16, 1);
        putWord(value, 18, 1);
        putDword(value, 28, 6);
        return value;
    }

    private static byte[] extendedReplay(int version, int rate, int frames) {
        byte[] track = extendedTrack();
        byte[] value = new byte[64 + track.length + frames];
        byte[] magic = {'C', 'S', 'R', 'P', 'L', '\r', '\n', 26};
        System.arraycopy(magic, 0, value, 0, magic.length);
        putWord(value, 8, version);
        putWord(value, 10, 64);
        putDword(value, 16, track.length);
        putDword(value, 20, frames);
        putWord(value, 24, rate);
        putWord(value, 26, GenerationRequest.RULE_OWOOT);
        byte[] car = {'_', 'C', 'S', 'T'};
        System.arraycopy(car, 0, value, 28, car.length);
        value[33] = 1;
        System.arraycopy(track, 0, value, 64, track.length);
        return value;
    }

    public static void main(String[] arguments) throws IOException {
        File game = new File(arguments[0]).getCanonicalFile();
        File source = new File(game, "source.RPL");
        File track = new File(game, "source.TRK");
        File output = new File(game, "ghost.RPL");
        File executable = new File(game, "genghost");
        Files.write(source.toPath(), replay());
        Files.write(track.toPath(), new byte[1802]);

        for (String mode : new String[] {GenerationRequest.MODE_FINE_TUNE,
                GenerationRequest.MODE_GEARS_ONLY}) {
            for (String ignoredCar : new String[] {"", "COUN", "invalid"}) {
                GenerationRequest value = request(source.getName(), ignoredCar, mode);
                assert value.validate(game).equals(source);
                assert value.resolvedCar(source).equals("PMIN");
                assert value.fineTune;
                assert value.fineTuneGearsOnly == mode.equals(GenerationRequest.MODE_GEARS_ONLY);
                List<String> command = value.arguments(executable, game, source, output);
                assert command.get(command.indexOf(source.getAbsolutePath()) + 1).equals("--seconds");
                assert !command.contains("--transmission");
                assert command.contains("--fine-tune") != command.contains("--fine-tune-gears-only");
                assert command.contains(value.fineTuneGearsOnly
                    ? "--fine-tune-gears-only" : "--fine-tune");
                Intent transport = new Intent();
                value.putInto(transport);
                GenerationRequest restored = GenerationRequest.from(transport);
                assert restored.mode.equals(mode) && restored.fineTune == value.fineTune;
                assert restored.fineTuneGearsOnly == value.fineTuneGearsOnly;
                assert restored.arguments(executable, game, source, output).equals(command);
            }
            rejected(() -> request(track.getName(), "COUN", mode).validate(game));
        }

        for (String flag : new String[] {"--cfg:on", "--cfg:off"}) {
            GenerationRequest chicane = new GenerationRequest(track.getName(), "COUN", 60, 2,
                "state", GenerationRequest.MODE_GENERATE, new String[] {flag, "--amigagrass", "--owoot"});
            assert chicane.validate(game).equals(track);
            List<String> command = chicane.arguments(executable, game, track, output);
            assert command.contains(flag);
            Intent transport = new Intent();
            chicane.putInto(transport);
            assert GenerationRequest.from(transport).arguments(executable, game, track, output)
                .equals(command);
        }
        for (String[] flags : new String[][] {{"--cfg:maybe"}, {"--cfg:offx"},
                {"--cfg:on", "--cfg:off"}}) {
            rejected(() -> new GenerationRequest(track.getName(), "COUN", 60, 2, "state",
                GenerationRequest.MODE_GENERATE, flags).validate(game));
        }

        GenerationRequest generation = request(track.getName(), "coun", GenerationRequest.MODE_GENERATE);
        assert generation.validate(game).equals(track);
        assert generation.resolvedCar(track).equals("COUN");
        assert !generation.fineTune && !generation.fineTuneGearsOnly;
        List<String> generated = generation.arguments(executable, game, track, output);
        assert generated.get(generated.indexOf(track.getAbsolutePath()) + 1).equals("COUN");
        assert !generated.contains("--fine-tune") && !generated.contains("--fine-tune-gears-only");
        assert !generated.contains("--transmission");
        GenerationRequest custom = request(track.getName(), "_cst", GenerationRequest.MODE_GENERATE);
        assert custom.validate(game).equals(track);
        assert custom.resolvedCar(track).equals("_CST");
        byte[] customReplay = replay();
        byte[] customCode = custom.car.getBytes(java.nio.charset.StandardCharsets.US_ASCII);
        System.arraycopy(customCode, 0, customReplay, 0, customCode.length);
        Files.write(source.toPath(), customReplay);
        assert request(source.getName(), "", GenerationRequest.MODE_FINE_TUNE)
            .resolvedCar(source).equals("_CST");
        Files.write(source.toPath(), replay());
        rejected(() -> request(track.getName(), "", GenerationRequest.MODE_GENERATE).validate(game));
        rejected(() -> request(track.getName(), "COUN", "fine-tune,gears-only").validate(game));

        for (int invalid : new int[] {0, 5, 6, 22, 23, 24}) {
            byte[] broken = replay();
            broken[invalid] = (byte)(invalid == 0 ? '?' : invalid == 24 ? 0 : 9);
            Files.write(source.toPath(), broken);
            rejected(() -> request(source.getName(), "COUN", GenerationRequest.MODE_FINE_TUNE)
                .validate(game));
        }
        Files.write(source.toPath(), new byte[25]);
        rejected(() -> request(source.getName(), "COUN", GenerationRequest.MODE_GEARS_ONLY)
            .validate(game));
        Files.write(source.toPath(), new byte[1828]);
        rejected(() -> request(source.getName(), "COUN", GenerationRequest.MODE_FINE_TUNE)
            .validate(game));
        Files.write(source.toPath(), replay());
        File outside = new File(game.getParentFile(), "outside.RPL");
        Files.write(outside.toPath(), replay());
        rejected(() -> request(outside.getAbsolutePath(), "COUN", GenerationRequest.MODE_FINE_TUNE)
            .validate(game));
        File extendedTrack = new File(game, "wide.CSTRK");
        File extendedReplay = new File(game, "wide.CSRPL");
        Files.write(extendedTrack.toPath(), extendedTrack());
        assert request(extendedTrack.getName(), "COUN", GenerationRequest.MODE_GENERATE)
            .validate(game).equals(extendedTrack);
        assert GenerationRequest.outputExtension(extendedTrack).equals(".csrpl");
        assert GenerationRequest.outputExtension(source).equals(".rpl");
        for (int version : new int[] {11, 101}) {
            for (int rate : new int[] {10, 20}) {
                Files.write(extendedReplay.toPath(), extendedReplay(version, rate, 70001));
                for (String mode : new String[] {GenerationRequest.MODE_FINE_TUNE,
                        GenerationRequest.MODE_GEARS_ONLY}) {
                    GenerationRequest value = request(extendedReplay.getName(), "COUN", mode);
                    assert value.validate(game).equals(extendedReplay);
                    assert value.resolvedCar(extendedReplay).equals("_CST");
                }
                GenerationRequest.ReplayInfo info = GenerationRequest.readReplay(extendedReplay);
                assert info.extended && info.frames == 70001 && info.rate == rate;
                assert info.rules == GenerationRequest.RULE_OWOOT;
                assert request(extendedReplay.getName(), "COUN", GenerationRequest.MODE_GENERATE)
                    .validate(game).equals(extendedReplay);
            }
        }
        for (int offset : new int[] {10, 12, 16, 24, 26, 33, 34, 40, 50, 64, 80}) {
            byte[] invalid = extendedReplay(11, 20, 4);
            invalid[offset] = (byte)255;
            Files.write(extendedReplay.toPath(), invalid);
            rejected(() -> request(extendedReplay.getName(), "COUN", GenerationRequest.MODE_FINE_TUNE)
                .validate(game));
        }
        byte[] invalidInput = extendedReplay(11, 20, 4);
        invalidInput[invalidInput.length - 1] = 64;
        Files.write(extendedReplay.toPath(), invalidInput);
        rejected(() -> GenerationRequest.readReplay(extendedReplay));
        Files.write(extendedReplay.toPath(), extendedReplay(10, 20, 4));
        rejected(() -> GenerationRequest.readReplay(extendedReplay));
        Files.write(extendedReplay.toPath(), extendedReplay(11, 20,
            GenerationRequest.EXTENDED_REPLAY_MAX_FRAMES + 1));
        rejected(() -> GenerationRequest.readReplay(extendedReplay));
        Files.write(extendedTrack.toPath(), new byte[38]);
        rejected(() -> request(extendedTrack.getName(), "COUN", GenerationRequest.MODE_GENERATE)
            .validate(game));
        for (String flag : new String[] {"--owoot:off", "--amigagrass:off"}) {
            GenerationRequest explicitOff = new GenerationRequest(track.getName(), "COUN", 60, 2,
                "state", GenerationRequest.MODE_GENERATE, new String[] {flag});
            assert explicitOff.validate(game).equals(track);
        }
        System.out.println("GenerationRequestTest passed: exclusive modes, replay car, validation, and arguments");
    }
}
