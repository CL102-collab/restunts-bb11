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
        System.out.println("GenerationRequestTest passed: exclusive modes, replay car, validation, and arguments");
    }
}
