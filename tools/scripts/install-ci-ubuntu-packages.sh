#!/usr/bin/env bash
set -euo pipefail

packages=("$@")
apt_options=(
    -o Acquire::Retries=1
    -o Acquire::http::Timeout=10
    -o Acquire::https::Timeout=10
)

install_packages() {
    sudo apt-get "${apt_options[@]}" "$@" -o APT::Update::Error-Mode=any update &&
        sudo apt-get "${apt_options[@]}" "$@" install -y --no-install-recommends "${packages[@]}"
}

if install_packages; then
    exit 0
fi

# shellcheck source=/dev/null
source /etc/os-release
if [[ "${ID:-}" != ubuntu || -z "${VERSION_CODENAME:-}" ]]; then
    echo 'The fallback package archive requires an Ubuntu release codename.' >&2
    exit 1
fi

# Use a separate HTTPS archive for both metadata and packages. Keep the
# runner's source configuration and Ubuntu archive signature checks intact.
fallback_sources=$(mktemp --suffix=.sources)
trap 'rm -f -- "$fallback_sources"' EXIT
cat > "$fallback_sources" <<EOF
Types: deb
URIs: https://azure.archive.ubuntu.com/ubuntu/
Suites: $VERSION_CODENAME $VERSION_CODENAME-updates $VERSION_CODENAME-backports $VERSION_CODENAME-security
Components: main restricted universe multiverse
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg
EOF

echo 'Retrying Ubuntu package installation with the Azure archive mirror.' >&2
install_packages -o "Dir::Etc::sourcelist=$fallback_sources" -o Dir::Etc::sourceparts=- \
    -o APT::Get::List-Cleanup=false
