"""Linux-hosted compiler and wx-config entry points in a prepared Haiku SDK."""

import os
from pathlib import Path
import re
import subprocess
import sys


def main():
    sdk = Path(__file__).resolve().parents[1]
    sysroot = sdk / "sysroot"
    program = os.environ.pop("RESTUNTS_HAIKU_DRIVER", Path(sys.argv[0]).name)
    target = (sdk / "restunts-target").read_text().strip()
    machine = (sdk / "restunts-machine").read_text().strip()
    arguments = sys.argv[1:]
    if program == "wx-config":
        # wx-config may derive the selected configuration from its own filename
        # and directory. Retain the imported script's native location and name.
        script = (sdk / "libexec/wx-config-target-path").read_text().strip()
        command = ["sh", str(sysroot / script), *arguments]
        result = subprocess.run(command, check=False, stdout=subprocess.PIPE)
        output = result.stdout.decode()
        output = re.sub(r"/packages/[^/\s\"']+/(?:\.self|[^/\s\"']+~[^/\s\"']+)(?=/|[^\w.-]|$)",
                        "/boot/system", output)
        output = re.sub(r"(?<![\w/.-])/system(?=/|[^\w.-]|$)", "/boot/system", output)
        output = re.sub(r"(-[IL])/system(?=/|[^\w.-]|$)", r"\1/boot/system", output)
        rooted_prefix = str(sysroot) + "/boot/system"
        placeholder = "\0RESTUNTS_HAIKU_PREFIX\0"
        output = output.replace(rooted_prefix, placeholder)
        output = output.replace("/boot/system", rooted_prefix)
        output = output.replace(placeholder, rooted_prefix)
        # Link-time include/library paths use the SDK. Runtime search paths
        # embedded by wxQt must continue to name files on the target system.
        for option in ("-Wl,-rpath,", "-Wl,-rpath=", "-Wl,--rpath,", "-Wl,--rpath=",
                       "-Wl,-R,", "-Wl,-R"):
            output = output.replace(option + str(sysroot), option)
        sys.stdout.write(output)
        return result.returncode

    compiler = "gcc" if program == "cc" else "g++"
    runtime = sysroot / "boot/system/lib"
    if target == "haiku-x86":
        runtime /= "x86"
    command = [str(sdk / "toolchain/bin" / (machine + "-" + compiler)),
               "--sysroot=" + str(sysroot),
               "-specs=" + str(sdk / "libexec/haiku-runtime.specs"), "-L" + str(runtime),
               "-Wl,-rpath-link," + str(runtime), *arguments]
    if arguments == ["-print-search-dirs"] and target == "haiku-x86":
        result = subprocess.run(command, check=False, stdout=subprocess.PIPE)
        for line in result.stdout.decode().splitlines():
            if line.startswith("libraries:"):
                # CMake's Haiku platform detects the modern secondary ABI from
                # this native target-path suffix. This is discovery metadata;
                # the compiler itself still searches the rooted paths above.
                line += ":/boot/system/develop/lib/x86/"
            print(line)
        return result.returncode
    os.execv(command[0], command)


if __name__ == "__main__":
    sys.exit(main())
