#!/usr/bin/perl
# Use the host package resolver to cache a foreign SDK without installing it.
use v5.36;
no lib ('/usr/local/libdata/perl5/site_perl');

use File::Temp;

package Restunts::OpenBSDSdk;

sub configure_download_only()
{
    require OpenBSD::AddDelete;
    require OpenBSD::PackingList;
    # OpenBSD 7.9's dry-run cache retains libraries and dependencies, but drops
    # tag definitions. Dependent packages then cannot resolve @tag actions even
    # though their defining packages have already been verified and cached.
    push @OpenBSD::PackingList::cache_categories, 'tags_definitions'
        unless grep { $_ eq 'tags_definitions' } @OpenBSD::PackingList::cache_categories;

    # Even with -n, LdConfig::init chroots to run the target's ldconfig -r.
    # An SDK does not need a runtime linker cache. Library dependency checking
    # uses SharedLibs independently and remains enabled, as do signature checks.
    no warnings qw(redefine once);
    *OpenBSD::LdConfig::init = sub($self) {
        die "SDK package resolver requires dry-run mode\n" unless $self->{state}{not};
        $self->{path} = {};
        $self->{todo} = 0;
    };
}

sub download_packages($architecture, $sysroot, @packages)
{
    # -B roots target files and shared-library lookups, but PackageInfo still
    # reads the host package database unless PKG_DBDIR is set before it loads.
    # Start with an empty database so host tools cannot satisfy target packages.
    my $package_db = File::Temp->newdir('restunts-openbsd-pkgdb.XXXXXXXX', TMPDIR => 1);
    local $ENV{PKG_DBDIR} = "$package_db/";
    configure_download_only();
    require OpenBSD::PkgAdd;
    local @ARGV = ('-n', '-I', '-A', $architecture, '-B', $sysroot, @packages);
    return OpenBSD::PkgAdd->parse_and_run('pkg_add');
}

unless (caller) {
    my ($architecture, $sysroot, @packages) = @ARGV;
    die "Usage: download-openbsd-sdk-packages.pl ARCH SYSROOT PACKAGE ...\n"
        unless defined $sysroot && @packages;
    exit download_packages($architecture, $sysroot, @packages);
}

1;
