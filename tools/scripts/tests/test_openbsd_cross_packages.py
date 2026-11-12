"""Check script-free OpenBSD SDK extraction and packing-list path handling."""

import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "extract-openbsd-sdk-packages.py"
SPEC = importlib.util.spec_from_file_location("openbsd_cross_packages", SCRIPT)
PACKAGES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGES)
DOWNLOADER = SCRIPT.with_name("download-openbsd-sdk-packages.pl")
PREPARER = SCRIPT.with_name("prepare-openbsd-cross.sh")
DEPENDENCY_PROBE_EXIT = 37


class OpenbsdSdkHostTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="OpenBSD SDK host with spaces ")
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)
        commands = self.directory / "commands"
        commands.mkdir()
        uname = commands / "uname"
        uname.write_text('''#!/bin/sh
case "$1" in
    -s) printf '%s\\n' OpenBSD ;;
    -m) printf '%s\\n' "$SDK_HOST_ARCH" ;;
    -r) printf '%s\\n' "$SDK_HOST_RELEASE" ;;
    *) exit 1 ;;
esac
''')
        uname.chmod(0o755)
        pkg_add = commands / "pkg_add"
        pkg_add.write_text('''#!/bin/sh
printf '%s\\n' "$@" > "$SDK_HOST_DEPENDENCIES"
exit "$SDK_DEPENDENCY_PROBE_EXIT"
''')
        pkg_add.chmod(0o755)
        self.record = self.directory / "dependencies"
        self.environment = dict(os.environ, PATH=str(commands) + os.pathsep + os.environ["PATH"],
                                SDK_HOST_ARCH="amd64", SDK_HOST_DEPENDENCIES=str(self.record),
                                SDK_DEPENDENCY_PROBE_EXIT=str(DEPENDENCY_PROBE_EXIT))

    def prepare(self, target, release, architecture="amd64"):
        self.record.unlink(missing_ok=True)
        environment = dict(self.environment, SDK_HOST_RELEASE=release, SDK_HOST_ARCH=architecture)
        return subprocess.run(["sh", str(PREPARER), target, str(self.directory / "target SDK")],
                              env=environment, text=True, capture_output=True,
                              timeout=15, check=False)

    def test_matching_release_selects_available_host_compiler_package(self):
        for target, release, compiler in (("openbsd-x86", "8.0", "llvm%22"),
                                          ("openbsd-arm64", "7.9", "llvm%19"),
                                          ("openbsd-riscv64", "7.9", "llvm%19")):
            with self.subTest(target=target):
                result = self.prepare(target, release)
                self.assertEqual(result.returncode, DEPENDENCY_PROBE_EXIT,
                                 result.stdout + result.stderr)
                self.assertIn(compiler, self.record.read_text().splitlines())

    def test_mismatched_release_is_rejected_before_installing_host_dependencies(self):
        for target, host_release in (("openbsd-x86", "7.9"),
                                     ("openbsd-arm64", "8.0"),
                                     ("openbsd-riscv64", "8.0")):
            with self.subTest(target=target):
                result = self.prepare(target, host_release)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("requires an OpenBSD", result.stderr)
                self.assertFalse(self.record.exists())

    def test_i386_host_is_rejected_before_installing_host_dependencies(self):
        result = self.prepare("openbsd-x86", "8.0", "i386")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("amd64 host", result.stderr)
        self.assertFalse(self.record.exists())


class OpenbsdSdkDownloaderTests(unittest.TestCase):
    def test_direct_download_ignores_host_database_before_loading_package_modules(self):
        perl = shutil.which("perl")
        if perl is None:
            self.skipTest("Perl is needed for the SDK downloader check")
        with tempfile.TemporaryDirectory(prefix="OpenBSD resolver with spaces ") as directory:
            root = Path(directory)
            host_database = root / "host packages"
            host_package = host_database / "libffi-host"
            host_package.mkdir(parents=True)
            (host_package / "+CONTENTS").write_text("host package must remain untouched\n")
            modules = root / "modules/OpenBSD"
            modules.mkdir(parents=True)
            # PackageInfo captures PKG_DBDIR when loaded, as the native module
            # does. A database change after importing it cannot isolate the SDK.
            (modules / "PackageInfo.pm").write_text(r'''
use v5.36;
package OpenBSD::PackageInfo;
our $database = $ENV{PKG_DBDIR};
opendir my $db, $database or die "Missing package database: $!\n";
our @installed = grep { !/^\./ } readdir $db;
closedir $db;
1;
''')
            (modules / "AddDelete.pm").write_text(r'''
use v5.36;
package OpenBSD::AddDelete;
use OpenBSD::PackageInfo;
package OpenBSD::LdConfig;
sub init($self) { die "Target program execution attempted\n"; }
1;
''')
            (modules / "PackingList.pm").write_text(r'''
use v5.36;
package OpenBSD::PackingList;
use OpenBSD::PackageInfo;
our @cache_categories = ('libraries', 'dependencies');
1;
''')
            (modules / "PkgAdd.pm").write_text(r'''
use v5.36;
use JSON::PP;
use OpenBSD::PackageInfo;
package OpenBSD::PkgAdd;
no warnings 'once';
sub parse_and_run($class, $command)
{
    # A host-installed dependency would suppress the foreign package download.
    if (!grep { /^libffi-/ } @OpenBSD::PackageInfo::installed) {
        open my $package, '>', "$ENV{PKG_CACHE}/libffi-target.tgz" or die $!;
        print $package "foreign package fixture\n";
        close $package;
    }
    open my $record, '>', $ENV{SDK_DB_RECORD} or die $!;
    print $record JSON::PP->new->encode({
        command => $command, arguments => [@ARGV],
        database => $OpenBSD::PackageInfo::database,
        installed => [@OpenBSD::PackageInfo::installed],
        cache_categories => [@OpenBSD::PackingList::cache_categories],
    });
    close $record;
    return 0;
}
1;
''')
            sdk = root / "target SDK"
            sdk.mkdir()
            for architecture in ("i386", "aarch64", "riscv64"):
                with self.subTest(architecture=architecture):
                    cache = root / (architecture + " cache")
                    cache.mkdir()
                    record = root / (architecture + ".json")
                    environment = dict(os.environ, PKG_DBDIR=str(host_database),
                                       PKG_CACHE=str(cache), SDK_DB_RECORD=str(record))
                    result = subprocess.run([
                        perl, "-I", str(modules.parent), str(DOWNLOADER),
                        architecture, str(sdk), "gtk+3", "extra-fixture",
                    ], env=environment, text=True, capture_output=True, timeout=15, check=False)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertEqual(result.stderr, "")
                    observed = json.loads(record.read_text())
                    self.assertEqual(observed["command"], "pkg_add")
                    self.assertEqual(observed["arguments"],
                                     ["-n", "-I", "-A", architecture, "-B", str(sdk),
                                      "gtk+3", "extra-fixture"])
                    self.assertEqual(observed["installed"], [])
                    self.assertIn("tags_definitions", observed["cache_categories"])
                    self.assertTrue((cache / "libffi-target.tgz").is_file())
                    database = Path(observed["database"])
                    self.assertNotEqual(database, host_database)
                    self.assertTrue(observed["database"].endswith("/"))
                    self.assertFalse(database.exists(), "Resolver database was not cleaned up")
            self.assertEqual((host_package / "+CONTENTS").read_text(),
                             "host package must remain untouched\n")
            self.assertEqual(list(sdk.iterdir()), [])


class OpenbsdSdkResolverTests(unittest.TestCase):
    """Exercise the native package metadata/cache code, when it is available."""

    @classmethod
    def setUpClass(cls):
        cls.perl = shutil.which("perl")
        if cls.perl is None:
            raise unittest.SkipTest("Perl is needed for OpenBSD package resolver checks")
        probe = subprocess.run([
            cls.perl, "-MOpenBSD::AddDelete", "-MOpenBSD::PackingList",
            "-MOpenBSD::Dependencies::SolverBase", "-e", "1",
        ], text=True, capture_output=True, check=False)
        if probe.returncode:
            raise unittest.SkipTest("Native OpenBSD package modules are not installed")

    def run_perl(self, source):
        result = subprocess.run([
            self.perl, "-e",
            "use v5.36; require $ARGV[0]; "
            "require OpenBSD::Dependencies::SolverBase; "
            "Restunts::OpenBSDSdk::configure_download_only();\n" + source,
            str(DOWNLOADER),
        ], text=True, capture_output=True, timeout=15, check=False)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stderr, "")

    def test_native_package_info_starts_empty_instead_of_using_host_packages(self):
        with tempfile.TemporaryDirectory(prefix="OpenBSD native resolver ") as directory:
            root = Path(directory)
            host_database = root / "host packages"
            host_package = host_database / "libffi-3.5.2p0"
            host_package.mkdir(parents=True)
            (host_package / "+CONTENTS").write_text("@name libffi-3.5.2p0\n@cwd /usr/local\n")
            source = r'''
use v5.36;
require $ARGV[0];
# Keep the actual PackageInfo/AddDelete/PackingList code, replacing only the
# network resolver entry so this check needs no repository or signing key.
$INC{'OpenBSD/PkgAdd.pm'} = 'SDK database fixture';
my $database_seen;
no warnings 'once';
*OpenBSD::PkgAdd::parse_and_run = sub($class, $command) {
    $database_seen = $ENV{PKG_DBDIR};
    die "Resolver database disappeared too early\n" unless -d $database_seen;
    my @installed = OpenBSD::PackageInfo::installed_packages();
    die "Host packages satisfied target dependencies\n" if @installed;
    return 0;
};
die "Resolver failed\n"
    if Restunts::OpenBSDSdk::download_packages('aarch64', $ARGV[1], 'gtk+3');
die "Temporary resolver database was retained\n" if -d $database_seen;
'''
            result = subprocess.run([
                self.perl, "-e", source, str(DOWNLOADER), str(root / "SDK"),
            ], env=dict(os.environ, PKG_DBDIR=str(host_database)), text=True,
                capture_output=True, timeout=15, check=False)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(result.stderr, "")
            self.assertTrue(host_package.is_dir())

    def test_tag_provider_remains_resolvable_after_dry_run_registration(self):
        self.run_perl(r'''
my $contents = "\@name sdk-tag-provider-1.0\n"
    . "\@define-tag sdk-fixture at-end false\n\@cwd /usr/local\n";
open my $fh, '<', \$contents or die $!;
my $provider = OpenBSD::PackingList->read($fh);
$provider->to_cache;
my $cached = OpenBSD::PackingList->from_installation('sdk-tag-provider-1.0',
    \&OpenBSD::PackingList::DependOnly);
my $lookup = bless {}, 'OpenBSD::lookup::tag';
$lookup->find_in_plist($cached, 'sdk-tag-provider-1.0');
my $tag = OpenBSD::PackingElement::Tag->new('sdk-fixture');
my $state = bless {}, 'SdkTestState';
die "Tag provider disappeared after caching\n"
    unless $lookup->find_in_already_done(undef, $state, $tag) eq 'sdk-tag-provider-1.0';
die "Tag action was not retained\n"
    unless $tag->{definition_list}[0]->command eq 'false';
my $missing = OpenBSD::PackingElement::Tag->new('missing-sdk-tag');
die "Unknown tag was accepted\n"
    if $lookup->find_in_already_done(undef, $state, $missing);
sub SdkTestState::verbose($) { 0 }
''')

    def test_library_tracking_never_chroots_or_runs_target_ldconfig(self):
        self.run_perl(r'''
no warnings qw(redefine once);
local *OpenBSD::Paths::chroot = sub { die "Target execution attempted\n"; };
my $ldconfig = OpenBSD::LdConfig->new({not => 1, destdir => '/foreign-sdk'});
$ldconfig->mark_directory('/usr/local/lib/libfixture.so.1.0');
$ldconfig->ensure;
die "SDK requested a runtime cache update\n" if $ldconfig->{todo};
my $install = OpenBSD::LdConfig->new({not => 0, destdir => '/foreign-sdk'});
eval { $install->init; };
die "Adapter allowed installation\n" unless $@ =~ /requires dry-run mode/;
''')


class OpenbsdCrossPackagesTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="OpenBSD SDK with spaces ")
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)
        self.sdk = self.directory / "sdk"
        self.sdk.mkdir()
        self.package = self.directory / "package.tgz"

    def write_package(self, contents, files):
        with tarfile.open(self.package, "w:gz") as archive:
            for name, data in {"+CONTENTS": contents, **files}.items():
                if isinstance(data, tarfile.TarInfo):
                    archive.addfile(data)
                    continue
                data = data.encode("utf-8")
                member = tarfile.TarInfo(name)
                member.size = len(data)
                member.mode = 0o644
                archive.addfile(member, io.BytesIO(data))

    def archive_link(self, name, target, kind):
        member = tarfile.TarInfo(name)
        member.type = kind
        member.linkname = target
        return member

    def test_installs_headers_libraries_and_metadata_under_each_target_prefix(self):
        self.write_package("""@name sdk-1.0
@cwd /usr/local
include/gtk.h
@lib lib/libgtk-3.so.2201.0
@pkgconfig lib/pkgconfig/gtk+-3.0.pc
@cwd /usr/X11R6
include/X11/Xlib.h
""", {"include/gtk.h": "GTK header", "lib/libgtk-3.so.2201.0": "target library",
      "lib/pkgconfig/gtk+-3.0.pc": "prefix=/usr/local\n",
      "include/X11/Xlib.h": "X11 header"})
        PACKAGES.extract_package(self.sdk, self.package)
        self.assertEqual((self.sdk / "usr/local/include/gtk.h").read_text(), "GTK header")
        self.assertEqual((self.sdk / "usr/local/lib/libgtk-3.so.2201.0").read_text(),
                         "target library")
        self.assertEqual((self.sdk / "usr/local/lib/pkgconfig/gtk+-3.0.pc").read_text(),
                         "prefix=/usr/local\n")
        self.assertEqual((self.sdk / "usr/X11R6/include/X11/Xlib.h").read_text(), "X11 header")

    def test_restores_links_recorded_in_packing_list_instead_of_archive_members(self):
        self.write_package("""@cwd /usr/local
include/source.h
include/hard.h
@link include/source.h
include/relative.h
@symlink source.h
include/absolute.h
@symlink /usr/local/include/source.h
""", {"include/source.h": "target header"})
        PACKAGES.extract_package(self.sdk, self.package)
        include = self.sdk / "usr/local/include"
        for name in ("hard.h", "relative.h", "absolute.h"):
            self.assertEqual((include / name).read_text(), "target header")
        self.assertEqual((include / "hard.h").stat().st_ino,
                         (include / "source.h").stat().st_ino)
        self.assertEqual((include / "relative.h").readlink(), Path("source.h"))
        self.assertFalse((include / "absolute.h").readlink().is_absolute())

    def test_accepts_matching_archive_links_before_their_regular_payloads(self):
        arrow = "share/icons/Adwaita/cursors/arrow"
        default = "share/icons/Adwaita/cursors/default"
        self.write_package(f"""@cwd /usr/local
{arrow}
@symlink default
include/hard-relative.h
@link /usr/local/include/source.h
include/hard-absolute.h
@link include/source.h
include/source.h
{default}
""", {arrow: self.archive_link(arrow, "default", tarfile.SYMTYPE),
      "include/hard-relative.h": self.archive_link(
          "include/hard-relative.h", "include/./source.h", tarfile.LNKTYPE),
      "include/hard-absolute.h": self.archive_link(
          "include/hard-absolute.h", "/usr/local/include/source.h", tarfile.LNKTYPE),
      "include/source.h": "target header", default: "cursor data"})
        PACKAGES.extract_package(self.sdk, self.package)
        include = self.sdk / "usr/local/include"
        for name in ("hard-relative.h", "hard-absolute.h"):
            self.assertEqual((include / name).read_text(), "target header")
            self.assertEqual((include / name).stat().st_ino,
                             (include / "source.h").stat().st_ino)
        self.assertEqual((self.sdk / "usr/local" / arrow).readlink(), Path("default"))
        self.assertEqual((self.sdk / "usr/local" / arrow).read_text(), "cursor data")

    def test_rejects_undeclared_mismatched_and_wrong_type_archive_links(self):
        filename = "include/alias.h"
        for declaration, target, kind in (
                ("", "source.h", tarfile.SYMTYPE),
                ("@symlink source.h\n", "other.h", tarfile.SYMTYPE),
                ("@symlink source.h\n", "include/source.h", tarfile.LNKTYPE),
                ("@link /usr/local/include/source.h\n", "source.h", tarfile.SYMTYPE),
                ("@link /usr/local/include/source.h\n", "include/other.h", tarfile.LNKTYPE)):
            with self.subTest(declaration=declaration, target=target, kind=kind):
                self.write_package(f"@cwd /usr/local\n{filename}\n{declaration}",
                                   {filename: self.archive_link(filename, target, kind)})
                with self.assertRaises(ValueError):
                    PACKAGES.extract_package(self.sdk, self.package)
                self.assertFalse((self.sdk / "usr/local" / filename).exists())
        self.write_package(f"@cwd /usr/local\n{filename}\n@symlink source.h\n",
                           {filename: "regular data in place of a link"})
        with self.assertRaisesRegex(ValueError, "link differs from packing list"):
            PACKAGES.extract_package(self.sdk, self.package)

    def test_rejects_special_archive_members(self):
        filename = "include/special.h"
        member = tarfile.TarInfo(filename)
        member.type = tarfile.FIFOTYPE
        self.write_package(f"@cwd /usr/local\n{filename}\n", {filename: member})
        with self.assertRaisesRegex(ValueError, "Unexpected package payload"):
            PACKAGES.extract_package(self.sdk, self.package)
        self.assertFalse((self.sdk / "usr/local" / filename).exists())

    def test_archive_links_cannot_follow_existing_targets_outside_sdk(self):
        host_file = self.directory / "host.h"
        host_file.write_text("host data")
        include = self.sdk / "usr/local/include"
        include.mkdir(parents=True)
        (include / "source.h").symlink_to(host_file)
        filename = "include/alias.h"
        for directive, target, kind in (
                ("@symlink", "source.h", tarfile.SYMTYPE),
                ("@link", "/usr/local/include/source.h", tarfile.LNKTYPE)):
            with self.subTest(directive=directive):
                self.write_package(f"@cwd /usr/local\n{filename}\n{directive} {target}\n",
                                   {filename: self.archive_link(filename, target, kind)})
                with self.assertRaisesRegex(ValueError, "escapes the SDK"):
                    PACKAGES.extract_package(self.sdk, self.package)
                self.assertFalse((include / "alias.h").exists())
                self.assertEqual(host_file.read_text(), "host data")

    def test_accepts_absolute_rcscript_paths_but_does_not_run_package_actions(self):
        marker = self.directory / "package-action-ran"
        self.write_package(f"""@cwd /usr/local
@rcscript /etc/rc.d/messagebus
@exec touch {marker}
@define-tag sdk-test at-end touch {marker}
@tag sdk-test
""", {"/etc/rc.d/messagebus": "#!/bin/sh\nexit 42\n",
      "+INSTALL": f"#!/bin/sh\ntouch {marker}\n"})
        PACKAGES.extract_package(self.sdk, self.package)
        self.assertTrue((self.sdk / "etc/rc.d/messagebus").is_file())
        self.assertFalse(marker.exists())
        self.assertFalse((self.sdk / "+CONTENTS").exists())
        self.assertFalse((self.sdk / "+INSTALL").exists())

    def test_rejects_unlisted_payload_and_traversal_before_writing_outside_sdk(self):
        for contents, name in (("@cwd /usr/local\ninclude/known.h\n", "include/unknown.h"),
                               ("@cwd /usr/local\n../../escaped\n", "../../escaped"),
                               ("@cwd /usr/local\n/etc/escaped\n", "/etc/escaped")):
            with self.subTest(name=name):
                self.write_package(contents, {name: "unexpected"})
                with self.assertRaises(ValueError):
                    PACKAGES.extract_package(self.sdk, self.package)
        self.assertFalse((self.directory / "escaped").exists())

    def test_does_not_follow_existing_sdk_symlink_to_host_directory(self):
        outside = self.directory / "host"
        outside.mkdir()
        (self.sdk / "usr").mkdir()
        (self.sdk / "usr/local").symlink_to(outside, target_is_directory=True)
        self.write_package("@cwd /usr/local\nescaped.h\n", {"escaped.h": "unexpected"})
        with self.assertRaisesRegex(ValueError, "escapes the SDK"):
            PACKAGES.extract_package(self.sdk, self.package)
        self.assertFalse((outside / "escaped.h").exists())

    def test_normalizes_release_set_links_and_rejects_relative_escapes(self):
        library = self.sdk / "usr/lib/libfixture.so"
        library.parent.mkdir(parents=True)
        library.symlink_to("/lib/libfixture.so.1")
        PACKAGES.normalize_links(self.sdk)
        self.assertEqual(library.readlink(), Path("../../lib/libfixture.so.1"))
        library.unlink()
        library.symlink_to("../../../host-library")
        with self.assertRaisesRegex(ValueError, "escapes its root"):
            PACKAGES.normalize_links(self.sdk)


if __name__ == "__main__":
    unittest.main()
