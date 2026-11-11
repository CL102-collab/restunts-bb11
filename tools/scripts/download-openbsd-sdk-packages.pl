#!/usr/bin/perl
# Use the host package resolver to cache a foreign SDK without installing it.
use v5.36;
no lib ('/usr/local/libdata/perl5/site_perl');

use OpenBSD::AddDelete;
use OpenBSD::PackingList;

package Restunts::OpenBSDSdk;

sub configure_download_only()
{
    # OpenBSD 7.9's dry-run cache retains libraries and dependencies, but drops
    # tag definitions. Dependent packages then cannot resolve @tag actions even
    # though their defining packages have already been verified and cached.
    push @OpenBSD::PackingList::cache_categories, 'tags_definitions'
        unless grep { $_ eq 'tags_definitions' } @OpenBSD::PackingList::cache_categories;

    # Even with -n, LdConfig::init chroots to run the target's ldconfig -r.
    # An SDK does not need a runtime linker cache. Library dependency checking
    # uses SharedLibs independently and remains enabled, as do signature checks.
    no warnings 'redefine';
    *OpenBSD::LdConfig::init = sub($self) {
        die "SDK package resolver requires dry-run mode\n" unless $self->{state}{not};
        $self->{path} = {};
        $self->{todo} = 0;
    };
}

unless (caller) {
    require OpenBSD::PkgAdd;
    my ($architecture, $sysroot, @packages) = @ARGV;
    die "Usage: download-openbsd-sdk-packages.pl ARCH SYSROOT PACKAGE ...\n"
        unless defined $sysroot && @packages;
    configure_download_only();
    @ARGV = ('-n', '-I', '-A', $architecture, '-B', $sysroot, @packages);
    exit OpenBSD::PkgAdd->parse_and_run('pkg_add');
}

1;
