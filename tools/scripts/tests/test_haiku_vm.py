"""Exercise VM integrity checks and cleanup without booting or formatting disks."""

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock


SCRIPTS = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("haiku_vm", SCRIPTS / "haiku-vm.py")
VM = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VM)
FIXTURE_DATA = b"verified VM image fixture\n"
FIXTURE_DIGEST = hashlib.sha256(FIXTURE_DATA).hexdigest()
FIXTURE_NAME = "fixture.qcow2.zst"
WRONG_BOOT_DEVICE = "/dev/disk/ata/0/master/raw"
SCRIPT_MODE = 0o755
SHELL_TIMEOUT_SECONDS = 10
TEST_DISK_BYTES = 4096
CONFIGURE_FAILURE_CODE = 99


class HaikuVmTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)

    def boot_arguments(self):
        for name in (VM.KEY_NAME, VM.X86_DISK_NAME, "work-x86.qcow2"):
            (self.directory / name).write_bytes(FIXTURE_DATA)
        return argparse.Namespace(directory=self.directory, arch="x86", port=VM.DEFAULT_PORT,
                                  memory=VM.DEFAULT_MEMORY_MB, cpus=VM.DEFAULT_CPUS,
                                  vnc_display=None, accel="auto",
                                  boot_timeout=VM.BOOT_TIMEOUT_SECONDS,
                                  ssh_timeout=VM.SSH_TIMEOUT_SECONDS)

    def test_corrupt_cached_image_is_rejected_before_any_command(self):
        (self.directory / FIXTURE_NAME).write_bytes(b"corrupt cached image")
        with mock.patch.object(VM, "command") as command, \
                self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
            VM.download(self.directory, FIXTURE_NAME, expected=FIXTURE_DIGEST)
        command.assert_not_called()

    def test_guest_host_identity_is_separate_by_architecture_and_stable_across_ports(self):
        configuration = self.directory / "connection.json"
        identities = {}
        for architecture in ("x64", "x86"):
            for port in (VM.DEFAULT_PORT, VM.DEFAULT_PORT + 1):
                configuration.write_text(json.dumps({"port": port, "arch": architecture}))
                options = VM.ssh_options(self.directory)
                identity = next(option for option in options if option.startswith("HostKeyAlias="))
                if architecture in identities:
                    self.assertEqual(identity, identities[architecture])
                identities[architecture] = identity
        self.assertNotEqual(identities["x64"], identities["x86"])

    def test_existing_connection_keeps_its_original_host_key_lookup(self):
        (self.directory / "connection.json").write_text(json.dumps({"port": VM.DEFAULT_PORT}))
        options = VM.ssh_options(self.directory)
        self.assertFalse(any(option.startswith("HostKeyAlias=") for option in options))
        self.assertIn(f"UserKnownHostsFile={self.directory / 'known_hosts'}", options)

    def test_corrupt_download_is_never_promoted_to_cached_image(self):
        def corrupt_download(arguments):
            Path(arguments[-1]).write_bytes(b"corrupt download")

        with mock.patch.object(VM, "command", side_effect=corrupt_download), \
                self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
            VM.download(self.directory, FIXTURE_NAME, expected=FIXTURE_DIGEST)
        self.assertFalse((self.directory / FIXTURE_NAME).exists())

    def test_boot_timeout_stops_only_the_vm_it_started(self):
        arguments = self.boot_arguments()
        with mock.patch.object(VM.shutil, "which", return_value="available"), \
                mock.patch.object(VM, "download", return_value=self.directory / VM.KEY_NAME), \
                mock.patch.object(VM, "command"), \
                mock.patch.object(VM, "is_running", side_effect=[False, True, True]), \
                mock.patch.object(VM, "ssh", side_effect=subprocess.TimeoutExpired(
                    "ssh", VM.SSH_TIMEOUT_SECONDS)) as ssh, \
                mock.patch.object(VM.time, "monotonic", side_effect=[
                    0, 0, VM.BOOT_TIMEOUT_SECONDS]), \
                mock.patch.object(VM.time, "sleep"), \
                mock.patch.object(VM, "qmp") as qmp, \
                self.assertRaisesRegex(ValueError, "SSH did not become ready"):
            VM.start(arguments)
        self.assertEqual(ssh.call_count, 1)
        self.assertEqual(ssh.call_args.kwargs["timeout"], VM.SSH_TIMEOUT_SECONDS)
        qmp.assert_called_once_with(self.directory, "quit")

    def test_unexpected_boot_error_still_stops_the_started_vm(self):
        arguments = self.boot_arguments()
        with mock.patch.object(VM.shutil, "which", return_value="available"), \
                mock.patch.object(VM, "download", return_value=self.directory / VM.KEY_NAME), \
                mock.patch.object(VM, "command"), \
                mock.patch.object(VM, "is_running", side_effect=[False, True]), \
                mock.patch.object(VM, "ssh", side_effect=OSError("SSH unavailable")), \
                mock.patch.object(VM, "qmp") as qmp, \
                self.assertRaisesRegex(OSError, "SSH unavailable"):
            VM.start(arguments)
        qmp.assert_called_once_with(self.directory, "quit")

    def test_start_refuses_to_take_over_an_existing_vm(self):
        arguments = self.boot_arguments()
        with mock.patch.object(VM, "is_running", return_value=True), \
                mock.patch.object(VM, "command") as command, \
                mock.patch.object(VM, "qmp") as qmp, \
                self.assertRaisesRegex(ValueError, "already running"):
            VM.start(arguments)
        command.assert_not_called()
        qmp.assert_not_called()

    def test_software_emulation_does_not_use_accessible_kvm(self):
        arguments = self.boot_arguments()
        arguments.accel = "tcg"
        arguments.ssh_timeout = VM.SSH_TIMEOUT_SECONDS * 2
        with mock.patch.object(VM.shutil, "which", return_value="available"), \
                mock.patch.object(VM, "download", return_value=self.directory / VM.KEY_NAME), \
                mock.patch.object(VM, "command") as command, \
                mock.patch.object(VM, "is_running", return_value=False), \
                mock.patch.object(VM.os, "access", return_value=True), \
                mock.patch.object(VM, "ssh") as ssh:
            VM.start(arguments)
        qemu_arguments = command.call_args.args[0]
        self.assertEqual(qemu_arguments[qemu_arguments.index("-accel") + 1], "tcg")
        self.assertEqual(qemu_arguments[qemu_arguments.index("-cpu") + 1], VM.SOFTWARE_CPU)
        self.assertEqual(ssh.call_args.kwargs["timeout"], arguments.ssh_timeout)
        self.assertIn(f"ConnectTimeout={arguments.ssh_timeout}", VM.ssh_options(self.directory))

    def test_required_kvm_rejects_inaccessible_device_without_starting_qemu(self):
        arguments = self.boot_arguments()
        arguments.accel = "kvm"
        with mock.patch.object(VM.shutil, "which", return_value="available"), \
                mock.patch.object(VM, "download", return_value=self.directory / VM.KEY_NAME), \
                mock.patch.object(VM, "command") as command, \
                mock.patch.object(VM, "is_running", return_value=False), \
                mock.patch.object(VM.os, "access", return_value=False) as access, \
                mock.patch.object(VM, "ssh") as ssh, \
                self.assertRaisesRegex(ValueError, "KVM is not accessible"):
            VM.start(arguments)
        access.assert_called_once_with("/dev/kvm", os.R_OK | os.W_OK)
        command.assert_not_called()
        ssh.assert_not_called()

    def test_required_kvm_uses_hardware_acceleration_when_accessible(self):
        arguments = self.boot_arguments()
        arguments.accel = "kvm"
        with mock.patch.object(VM.shutil, "which", return_value="available"), \
                mock.patch.object(VM, "download", return_value=self.directory / VM.KEY_NAME), \
                mock.patch.object(VM, "command") as command, \
                mock.patch.object(VM, "is_running", return_value=False), \
                mock.patch.object(VM.os, "access", return_value=True) as access, \
                mock.patch.object(VM, "ssh") as ssh:
            VM.start(arguments)
        access.assert_called_once_with("/dev/kvm", os.R_OK | os.W_OK)
        command.assert_called_once()
        qemu_arguments = command.call_args.args[0]
        self.assertEqual(qemu_arguments[0], "qemu-system-x86_64")
        self.assertEqual(qemu_arguments[qemu_arguments.index("-accel") + 1], "kvm")
        self.assertEqual(qemu_arguments[qemu_arguments.index("-cpu") + 1], "host")
        ssh.assert_called_once()

    def test_failed_qemu_launch_stops_a_partially_started_daemon(self):
        arguments = self.boot_arguments()
        with mock.patch.object(VM.shutil, "which", return_value="available"), \
                mock.patch.object(VM, "download", return_value=self.directory / VM.KEY_NAME), \
                mock.patch.object(VM, "command", side_effect=subprocess.CalledProcessError(
                    1, "qemu-system-x86_64")), \
                mock.patch.object(VM, "is_running", side_effect=[False, True]), \
                mock.patch.object(VM, "qmp") as qmp, \
                self.assertRaises(subprocess.CalledProcessError):
            VM.start(arguments)
        qmp.assert_called_once_with(self.directory, "quit")

    def test_graceful_shutdown_does_not_force_quit(self):
        with mock.patch.object(VM, "ssh") as ssh, \
                mock.patch.object(VM, "is_running", side_effect=[True, False, False]), \
                mock.patch.object(VM.time, "monotonic", return_value=0), \
                mock.patch.object(VM.time, "sleep") as sleep, \
                mock.patch.object(VM, "qmp") as qmp:
            VM.stop(self.directory)
        self.assertEqual(ssh.call_args_list, [
            mock.call(self.directory, ["sync"], timeout=VM.STOP_TIMEOUT_SECONDS),
            mock.call(self.directory, ["shutdown", "-q"], timeout=VM.STOP_TIMEOUT_SECONDS),
        ])
        sleep.assert_called_once_with(VM.POLL_SECONDS)
        qmp.assert_not_called()

    def test_unresponsive_guest_shutdown_waits_for_qmp_exit(self):
        with mock.patch.object(VM, "ssh", side_effect=subprocess.TimeoutExpired(
                "ssh", VM.STOP_TIMEOUT_SECONDS)) as ssh, \
                mock.patch.object(VM, "is_running", side_effect=[
                    True, True, True, True, False]) as is_running, \
                mock.patch.object(VM.time, "monotonic", side_effect=[
                    0, 0, VM.STOP_TIMEOUT_SECONDS,
                    VM.STOP_TIMEOUT_SECONDS, VM.STOP_TIMEOUT_SECONDS]), \
                mock.patch.object(VM.time, "sleep") as sleep, \
                mock.patch.object(VM, "qmp") as qmp:
            VM.stop(self.directory)
        self.assertEqual(ssh.call_args.kwargs["timeout"], VM.STOP_TIMEOUT_SECONDS)
        qmp.assert_called_once_with(self.directory, "quit")
        self.assertEqual(is_running.call_count, 5)
        self.assertTrue(all(call == mock.call(self.directory)
                            for call in is_running.call_args_list))
        self.assertEqual(sleep.call_args_list, [
            mock.call(VM.POLL_SECONDS), mock.call(VM.POLL_SECONDS),
        ])

    def test_qmp_quit_timeout_fails_instead_of_reporting_stopped(self):
        with mock.patch.object(VM, "ssh"), \
                mock.patch.object(VM, "is_running", return_value=True), \
                mock.patch.object(VM.time, "monotonic", side_effect=[
                    0, VM.STOP_TIMEOUT_SECONDS,
                    VM.STOP_TIMEOUT_SECONDS, VM.STOP_TIMEOUT_SECONDS * 2]), \
                mock.patch.object(VM.time, "sleep"), \
                mock.patch.object(VM, "qmp") as qmp, \
                mock.patch.object(VM, "command") as command, \
                self.assertRaisesRegex(ValueError, "did not stop after QMP quit"):
            VM.stop(self.directory)
        qmp.assert_called_once_with(self.directory, "quit")
        command.assert_not_called()

    def test_prepare_refuses_to_overwrite_a_completed_disk(self):
        destination = self.directory / VM.X86_DISK_NAME
        destination.write_bytes(FIXTURE_DATA)
        with mock.patch.object(VM, "download") as download, \
                self.assertRaisesRegex(ValueError, "already exists"):
            VM.prepare_x86(argparse.Namespace(directory=self.directory, port=VM.DEFAULT_PORT,
                                              accel="auto", boot_timeout=VM.BOOT_TIMEOUT_SECONDS,
                                              cpus=VM.DEFAULT_CPUS,
                                              ssh_timeout=VM.SSH_TIMEOUT_SECONDS))
        download.assert_not_called()
        self.assertEqual(destination.read_bytes(), FIXTURE_DATA)

    def test_prepare_timeout_removes_incomplete_disk_and_stops_bootstrap(self):
        for name in (VM.KEY_NAME, VM.X86_IMAGE_NAME, VM.IMAGE_NAME):
            (self.directory / name).write_bytes(FIXTURE_DATA)
        with mock.patch.object(VM, "download", side_effect=lambda directory, name, *args:
                               directory / name), \
                mock.patch.object(VM, "command") as command, \
                mock.patch.object(VM, "start"), \
                mock.patch.object(VM, "stop") as stop, \
                mock.patch.object(VM, "ssh_options", return_value=[]), \
                mock.patch.object(VM, "is_running", side_effect=[False, True]), \
                mock.patch.object(VM, "ssh", side_effect=[None, subprocess.TimeoutExpired(
                    "prepare guest disk", VM.PREPARE_TIMEOUT_SECONDS)]) as ssh, \
                mock.patch.object(VM, "X86_DISK_BYTES", TEST_DISK_BYTES), \
                self.assertRaises(subprocess.TimeoutExpired):
            VM.prepare_x86(argparse.Namespace(directory=self.directory, port=VM.DEFAULT_PORT,
                                              accel="auto", boot_timeout=VM.BOOT_TIMEOUT_SECONDS,
                                              cpus=VM.DEFAULT_CPUS,
                                              ssh_timeout=VM.SSH_TIMEOUT_SECONDS))
        self.assertEqual(ssh.call_args.kwargs["timeout"], VM.PREPARE_TIMEOUT_SECONDS)
        stop.assert_called_once_with(self.directory / "bootstrap-x64")
        self.assertEqual(list(self.directory.glob("*.raw")), [])
        self.assertFalse((self.directory / VM.X86_DISK_NAME).exists())
        self.assertFalse(any(call.args[0][0] == "qemu-img" for call in command.call_args_list))

    def test_prepare_refuses_to_stop_an_existing_bootstrap_vm(self):
        source = self.directory / VM.X86_IMAGE_NAME
        source.write_bytes(FIXTURE_DATA)
        with mock.patch.object(VM, "download", return_value=source), \
                mock.patch.object(VM, "is_running", return_value=True), \
                mock.patch.object(VM, "stop") as stop, \
                self.assertRaisesRegex(ValueError, "already running"):
            VM.prepare_x86(argparse.Namespace(directory=self.directory, port=VM.DEFAULT_PORT,
                                              accel="auto", boot_timeout=VM.BOOT_TIMEOUT_SECONDS,
                                              cpus=VM.DEFAULT_CPUS,
                                              ssh_timeout=VM.SSH_TIMEOUT_SECONDS))
        stop.assert_not_called()

    def shell_environment(self, system):
        commands = self.directory / "commands"
        commands.mkdir(exist_ok=True)
        log = self.directory / "disk-commands.log"
        for name in ("mount", "mkfs", "makebootable", "pkgman", "cmake", "git"):
            script = commands / name
            script.write_text('#!/bin/sh\nprintf "%s\\n" "$0" >> "$COMMAND_LOG"\nexit 99\n')
            script.chmod(SCRIPT_MODE)
        uname = commands / "uname"
        uname.write_text(f"#!/bin/sh\nprintf '%s\\n' '{system}'\n")
        uname.chmod(SCRIPT_MODE)
        # The script's EXIT cleanup must not call a real host unmount command.
        unmount = commands / "unmount"
        unmount.write_text("#!/bin/sh\nexit 0\n")
        unmount.chmod(SCRIPT_MODE)
        return {**os.environ, "PATH": f"{commands}:{os.environ['PATH']}",
                "COMMAND_LOG": str(log)}, log

    def test_disk_preparation_rejects_boot_disk_before_mounting_or_formatting(self):
        environment, log = self.shell_environment("Haiku")
        for source, destination in ((WRONG_BOOT_DEVICE, VM.X86_TARGET_DEVICE),
                                    (VM.X86_SOURCE_DEVICE, WRONG_BOOT_DEVICE)):
            with self.subTest(source=source, destination=destination):
                result = subprocess.run(["sh", str(SCRIPTS / "prepare-haiku-x86-vm.sh"),
                                         source, destination, "unused-public-key"],
                                        env=environment, capture_output=True, text=True,
                                        timeout=SHELL_TIMEOUT_SECONDS)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("dedicated secondary target disk", result.stderr)
                self.assertFalse(log.exists())

    def test_native_helpers_reject_wrong_host_before_building_or_installing(self):
        environment, log = self.shell_environment("Linux")
        for script, shell in (("install-haiku-package-dependencies.sh", "sh"),
                              ("build-package.sh", "bash")):
            for target in ("haiku-x64", "haiku-x86"):
                with self.subTest(script=script, target=target):
                    result = subprocess.run([shell, str(SCRIPTS / script), target],
                                            env=environment, capture_output=True, text=True,
                                            timeout=SHELL_TIMEOUT_SECONDS)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("matching native Haiku userspace", result.stderr)
                    self.assertFalse(log.exists())

    def package_configuration(self, target, architecture, multiplier=None):
        checkout = self.directory / "checkout"
        scripts = checkout / "tools" / "scripts"
        scripts.mkdir(parents=True, exist_ok=True)
        package_script = scripts / "build-package.sh"
        package_script.write_bytes((SCRIPTS / package_script.name).read_bytes())
        environment, log = self.shell_environment("Haiku")
        commands = self.directory / "commands"
        (commands / "uname").write_text(
            "#!/bin/sh\ncase \"$1\" in\n"
            f"  -s) echo Haiku ;;\n  -m) echo {architecture} ;;\nesac\n")
        (commands / "git").write_text("#!/bin/sh\nexit 0\n")
        setarch = commands / "setarch"
        setarch.write_text('#!/bin/sh\nprintf "%s\\n" "$PATH"\n')
        setarch.chmod(SCRIPT_MODE)
        (commands / "cmake").write_text(
            '#!/bin/sh\nprintf "%s\\0" "$@" > "$COMMAND_LOG"\n'
            f"exit {CONFIGURE_FAILURE_CODE}\n")
        environment.pop("RESTUNTS_GENGHOST_TEST_TIMEOUT_MULTIPLIER", None)
        if multiplier is not None:
            environment["RESTUNTS_GENGHOST_TEST_TIMEOUT_MULTIPLIER"] = multiplier
        result = subprocess.run(["bash", str(package_script), target], env=environment,
                                capture_output=True, text=True, timeout=SHELL_TIMEOUT_SECONDS)
        self.assertEqual(result.returncode, CONFIGURE_FAILURE_CODE, result.stderr)
        return log.read_text().split("\0")[:-1]

    def test_haiku_package_passes_timeout_multiplier_to_ctest_configuration(self):
        for target, architecture in (("haiku-x64", "x86_64"), ("haiku-x86", "BePC")):
            for multiplier in (None, "4", "7"):
                with self.subTest(target=target, multiplier=multiplier):
                    arguments = self.package_configuration(target, architecture, multiplier)
                    self.assertIn("-DRESTUNTS_BUILD_TESTS=ON", arguments)
                    self.assertIn(
                        "-DRESTUNTS_GENGHOST_TEST_TIMEOUT_MULTIPLIER=" + (multiplier or "1"),
                        arguments)

    def test_other_package_targets_keep_their_existing_test_configuration(self):
        arguments = self.package_configuration("linux-x64", "x86_64", "7")
        self.assertIn("-DRESTUNTS_BUILD_TESTS=OFF", arguments)
        self.assertFalse(any(argument.startswith("-DRESTUNTS_GENGHOST_TEST_TIMEOUT_MULTIPLIER=")
                             for argument in arguments))


if __name__ == "__main__":
    unittest.main()
