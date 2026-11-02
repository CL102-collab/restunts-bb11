"""Exercise the host runner's complete source graph without compiling fixtures."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


REPOSITORY = Path(__file__).resolve().parents[3]
RUNNER = Path("src/restunts/tests/run-host-tests.sh")
GRAPH_PASSES = 2
GRAPH_TIMEOUT_SECONDS = 60
EXECUTABLE_MODE = 0o755
COMPILER = r'''#!/bin/bash
set -eu
arguments=("$@")
output=
while [[ "$#" -gt 0 ]]; do
    if [[ "$1" == -o ]]; then
        shift
        output="$1"
        break
    fi
    shift
done
# The runner first probes compiler warnings without requesting an executable.
if [[ -z "$output" ]]; then
    exit 0
fi
printf '%s\0' "${arguments[@]}" '' >> "$HOST_TEST_COMMAND_LOG"
printf '#!/bin/sh\nexit 0\n' > "$output"
chmod +x "$output"
'''


@unittest.skipUnless(os.name == "posix" and shutil.which("bash"), "requires Bash and symlinks")
class HostTestSourceTests(unittest.TestCase):
    def test_complete_graph_is_stable_deduplicated_and_preserves_spaces(self):
        with tempfile.TemporaryDirectory(prefix="host source graph ") as temporary:
            directory = Path(temporary)
            checkout = directory / "checkout with spaces"
            checkout.symlink_to(REPOSITORY, target_is_directory=True)
            compiler = directory / "compiler with spaces.sh"
            compiler.write_text(COMPILER)
            compiler.chmod(EXECUTABLE_MODE)
            graphs = []
            for pass_index in range(GRAPH_PASSES):
                log = directory / f"commands-{pass_index}.bin"
                environment = dict(os.environ, CC=str(compiler), CFLAGS="", LDFLAGS="",
                                   HOST_TEST_COMMAND_LOG=str(log))
                result = subprocess.run(["bash", str(checkout / RUNNER)], env=environment,
                                        capture_output=True, text=True, timeout=GRAPH_TIMEOUT_SECONDS)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                commands = [record.decode().split("\0")
                            for record in log.read_bytes().split(b"\0\0") if record]
                graph = []
                for command in commands:
                    arguments = command[:-2]
                    sources = [Path(argument) for argument in arguments
                               if not argument.startswith("-")]
                    self.assertTrue(all(source.is_file() for source in sources), command)
                    resolved = [source.resolve() for source in sources]
                    self.assertEqual(len(resolved), len(set(resolved)), command)
                    graph.append((Path(command[-1]).name, arguments))
                graphs.append(graph)
            self.assertEqual(graphs[0], graphs[1])
            self.assertTrue(graphs[0])
            decompression = next(arguments for name, arguments in graphs[0]
                                 if name == "test-amiga-decompression")
            sources = {Path(argument).resolve() for argument in decompression
                       if argument.endswith(".c")}
            core = REPOSITORY / "src/restunts/c"
            self.assertEqual(sources, {REPOSITORY / "src/restunts/tests/test-amiga-decompression.c",
                                      core / "fileio.c", core / "extended/extended_fileio.c",
                                      core / "file_car.c", core / "file_decomp.c"})
            for name, arguments in graphs[0]:
                if name == "test-audio-engine":
                    # The fixture includes these modules; do not link them again.
                    for module in ("audio_engine.c", "audio_channels.c", "audio_context.c"):
                        self.assertFalse(any(argument.endswith("/" + module)
                                             for argument in arguments))


if __name__ == "__main__":
    unittest.main()
