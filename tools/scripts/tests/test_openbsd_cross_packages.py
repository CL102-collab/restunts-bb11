"""Check script-free OpenBSD SDK extraction and packing-list path handling."""

import importlib.util
import io
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
