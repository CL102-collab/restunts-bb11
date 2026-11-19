#!/bin/sh
# Install native FreeBSD packages before Bash is available.
set -eu

: "${1:?Usage: install-freebsd-packages.sh PACKAGE...}"
channel=${RESTUNTS_FREEBSD_PACKAGE_CHANNEL:-}
case "$channel" in
    '') exec pkg install -y "$@" ;;
    latest|quarterly) ;;
    *) echo "Unsupported FreeBSD package channel: $channel" >&2; exit 1 ;;
esac

repository_dir=$(mktemp -d /tmp/restunts-freebsd-repo.XXXXXX)
trap 'rm -rf -- "$repository_dir"' 0
trap 'exit 1' 1 2 15
export FETCH_TIMEOUT=20 FETCH_RETRY=1
catalogue_timeout=2m
download_timeout=10m

# Avoid SRV routing to an unreachable mirror. PAO's public mirror name
# lacks TLS hostname coverage, so use its HTTP endpoint as a last resort.
# The ports catalogue and package hashes are verified on every route.
for repository in https://pkg.freebsd.org https://pkg0.nyi.freebsd.org http://pkg0.pao.freebsd.org; do
    echo "Fetching FreeBSD packages from $repository ($channel)" >&2
    cat > "$repository_dir/FreeBSD.conf" <<EOF
FreeBSD: {
  url: "$repository/\${ABI}/$channel",
  mirror_type: "none",
  signature_type: "fingerprints",
  fingerprints: "/usr/share/keys/pkg",
  enabled: yes
}
EOF
    if timeout "$catalogue_timeout" pkg -4 -R "$repository_dir" update -f &&
        timeout "$download_timeout" pkg -4 -R "$repository_dir" install -y -U -F "$@"; then
        # Use the same catalogue and verified cache for installation.
        pkg -4 -R "$repository_dir" install -y -U "$@"
        exit 0
    fi
done
echo "Could not download FreeBSD packages: $*" >&2
exit 1
