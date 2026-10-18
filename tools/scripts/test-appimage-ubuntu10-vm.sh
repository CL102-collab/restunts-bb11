#!/usr/bin/env bash
# Boot the original Ubuntu 10.04.4 desktop userspace and distribution kernel.
# The original small initramfs boots an ext3 disk containing the original rootfs.
# Keeping the desktop filesystem out of the initramfs avoids i386 lowmem limits.
set -euo pipefail
if (( $# != 3 )); then
    echo "Usage: $0 TARGET ARCHIVE GAME_DATA" >&2
    exit 2
fi
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd -- "$script_dir/../.." && pwd)
target=$1 archive=$(realpath -- "$2") data_dir=$(realpath -- "$3")
case "$target" in
    linux-appimage-x64) architecture=amd64 cpu=qemu64 ;;
    linux-appimage-x86) architecture=i386 cpu=qemu32 ;;
    linux-appimage-x86-no-sse2) architecture=i386 cpu=pentium3 ;;
    *) echo "Unknown AppImage target: $target" >&2; exit 2 ;;
esac
[[ -f "$archive" && -d "$data_dir" ]]
required_tools=(cpio gzip xz mkfs.ext3 qemu-system-x86_64 Xvfb xwininfo)
if [[ -z ${RESTUNTS_UBUNTU10_ROOTFS:-} ]]; then
    required_tools+=(curl bsdtar unsquashfs)
fi
for tool in "${required_tools[@]}"; do
    command -v "$tool" >/dev/null || { echo "Missing VM prerequisite: $tool" >&2; exit 1; }
done
root_command=()
if (( EUID != 0 )); then
    command -v sudo >/dev/null || { echo 'Preparing the original rootfs requires root or sudo.' >&2; exit 1; }
    root_command=(sudo -n)
fi
cache_dir=${RESTUNTS_APPIMAGE_TEST_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/restunts-appimage-tests}
result_dir=${RESTUNTS_APPIMAGE_TEST_RESULTS:-$repo_dir/dist/appimage-tests/$target/ubuntu10.04}
mkdir -p "$cache_dir" "$result_dir"
cache_dir=$(realpath -- "$cache_dir") result_dir=$(realpath -- "$result_dir")
work_dir=$(mktemp -d)
xvfb_pid='' qemu_pid=''
cleanup() {
    [[ -z "$qemu_pid" ]] || kill "$qemu_pid" 2>/dev/null || true
    [[ -z "$xvfb_pid" ]] || kill "$xvfb_pid" 2>/dev/null || true
    "${root_command[@]}" rm -rf -- "$work_dir"
}
trap cleanup EXIT

# These optional inputs allow repeating the same guest tests with a separately
# prepared Ubuntu 10.04 rootfs/kernel/original initrd, without a cache dependency.
if [[ -n ${RESTUNTS_UBUNTU10_ROOTFS:-} || -n ${RESTUNTS_UBUNTU10_KERNEL:-} || -n ${RESTUNTS_UBUNTU10_INITRD:-} ]]; then
    [[ -d ${RESTUNTS_UBUNTU10_ROOTFS:-} && -f ${RESTUNTS_UBUNTU10_KERNEL:-} && -f ${RESTUNTS_UBUNTU10_INITRD:-} ]]
    "${root_command[@]}" cp -a "$RESTUNTS_UBUNTU10_ROOTFS" "$work_dir/rootfs"
    cp "$RESTUNTS_UBUNTU10_KERNEL" "$work_dir/vmlinuz"
    cp "$RESTUNTS_UBUNTU10_INITRD" "$work_dir/original-initrd"
else
    iso_name="ubuntu-10.04.4-desktop-$architecture.iso"
    release_url=https://old-releases.ubuntu.com/releases/10.04.4
    # Original publisher checksums: $release_url/SHA256SUMS . Pin the
    # fixture here so a changed mirror or stale cache cannot change the OS.
    case "$architecture" in
        amd64) expected=837a6ec168913951bf6371a6df6837217790f4210045c9f991eab641533726fe ;;
        i386) expected=4c4c982beede1094bcb20f93ccd8f79f63dec35b17d3e4b877d620f9faa47c38 ;;
    esac
    iso="$cache_dir/$iso_name"
    if [[ ! -f "$iso" ]]; then
        curl --fail --location --retry 3 --proto '=https' --proto-redir '=https' \
            "$release_url/$iso_name" -o "$iso.partial"
        mv "$iso.partial" "$iso"
    fi
    printf '%s  %s\n' "$expected" "$iso" | sha256sum --check
    printf '%s  %s\n' "$expected" "$iso_name" > "$result_dir/ubuntu-iso.sha256"
    bsdtar -xOf "$iso" casper/filesystem.squashfs > "$work_dir/filesystem.squashfs"
    bsdtar -xOf "$iso" casper/vmlinuz > "$work_dir/vmlinuz"
    bsdtar -xOf "$iso" casper/initrd.lz > "$work_dir/original-initrd"
    "${root_command[@]}" unsquashfs -no-progress -d "$work_dir/rootfs" "$work_dir/filesystem.squashfs"
    rm "$work_dir/filesystem.squashfs"
fi
rootfs="$work_dir/rootfs"
"${root_command[@]}" mkdir -p "$rootfs/compat/data" "$rootfs/proc" "$rootfs/sys" "$rootfs/dev"
"${root_command[@]}" cp "$archive" "$rootfs/compat/release.tar.gz"
"${root_command[@]}" cp -R "$data_dir/." "$rootfs/compat/data/"
"${root_command[@]}" cp "$script_dir/test-appimage-guest.sh" "$rootfs/compat/test-appimage-guest.sh"
"${root_command[@]}" test -c "$rootfs/dev/console" || "${root_command[@]}" mknod -m 600 "$rootfs/dev/console" c 5 1

display_number=$((100 + $$ % 500))
Xvfb ":$display_number" -screen 0 1024x768x24 -ac -listen tcp -fbdir "$result_dir" \
    > "$result_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
for ((attempt = 0; attempt < 50; attempt++)); do
    if DISPLAY=":$display_number" xwininfo -root >/dev/null 2>&1; then break; fi
    sleep 0.1
done
DISPLAY=":$display_number" xwininfo -root >/dev/null
no_sse2=0
[[ "$target" != linux-appimage-x86-no-sse2 ]] || no_sse2=1
cat > "$work_dir/init" <<EOF
#!/bin/dash
export PATH=/usr/sbin:/usr/bin:/sbin:/bin HOME=/root
mount -n -t proc proc /proc
mount -n -t sysfs sysfs /sys
mount -n -t devtmpfs devtmpfs /dev || mount -n -t tmpfs tmpfs /dev
mkdir -p /dev/pts /tmp /run
mount -t devpts devpts /dev/pts
[ -c /dev/null ] || mknod -m 666 /dev/null c 1 3
[ -c /dev/zero ] || mknod -m 666 /dev/zero c 1 5
[ -c /dev/random ] || mknod -m 666 /dev/random c 1 8
[ -c /dev/urandom ] || mknod -m 666 /dev/urandom c 1 9
[ -c /dev/fuse ] || mknod -m 666 /dev/fuse c 10 229
modprobe fuse
modprobe e1000
ifconfig eth0 10.0.2.15 netmask 255.255.255.0 up
export DISPLAY=10.0.2.2:$display_number SDL_VIDEODRIVER=x11 SDL_RENDER_DRIVER=software
export RESTUNTS_REQUIRE_NO_SSE2=$no_sse2 RESTUNTS_EXPECT_FUSE=1
printf '\nRESTUNTS_VM_BOOTED\n'
uname -a
cat /proc/cpuinfo
bash /compat/test-appimage-guest.sh '$target' /compat/release.tar.gz /compat/data ubuntu10.04
test_status=\$?
printf '\nRESTUNTS_VM_FINISHED status=%s\n' "\$test_status"
sync
poweroff -f
EOF
"${root_command[@]}" cp "$work_dir/init" "$rootfs/init"
"${root_command[@]}" chmod 755 "$rootfs/init"
# The desktop rootfs is several GiB uncompressed. A disk keeps it outside the
# old i386 kernel's limited lowmem region and leaves room for extracted packages.
rootfs_bytes=$("${root_command[@]}" du -s --block-size=1 "$rootfs" | cut -f1)
truncate -s "$((rootfs_bytes + 1073741824))" "$work_dir/rootfs.ext3"
"${root_command[@]}" mkfs.ext3 -q -F -O none,has_journal -I 128 -d "$rootfs" "$work_dir/rootfs.ext3"

# Preserve the ISO's matching early userspace and storage/filesystem modules.
# Only its init script changes: mount our disk and enter the original rootfs.
"${root_command[@]}" mkdir "$work_dir/initramfs"
case "$(od -An -tx1 -N2 "$work_dir/original-initrd" | tr -d ' \n')" in
    1f8b) gzip -dc "$work_dir/original-initrd" > "$work_dir/original-initrd.cpio" ;;
    *) xz --format=lzma -dc "$work_dir/original-initrd" > "$work_dir/original-initrd.cpio" ;;
esac
# shellcheck disable=SC2016 # Positional parameters belong to the child shell.
"${root_command[@]}" bash -c 'cd "$1"; cpio -id --quiet --no-absolute-filenames < "$2"' \
    bash "$work_dir/initramfs" "$work_dir/original-initrd.cpio"
echo 'Original initramfs run-init candidates:'
# Inspect links without following guest-absolute targets in the host root.
"${root_command[@]}" find "$work_dir/initramfs" -name run-init -ls
cat > "$work_dir/disk-init" <<'EOF'
#!/bin/sh
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
run_init=$(command -v run-init) || {
    echo 'RESTUNTS_VM_RUN_INIT_MISSING: original run-init is not executable on the guest PATH'
    ls -l /bin/run-init /sbin/run-init /usr/bin/run-init /usr/sbin/run-init
    poweroff -f
    exit 1
}
mount -n -t proc proc /proc
mount -n -t sysfs sysfs /sys
mount -n -t devtmpfs devtmpfs /dev || mount -n -t tmpfs tmpfs /dev
[ -c /dev/console ] || mknod -m 600 /dev/console c 5 1
[ -c /dev/sda ] || mknod -m 600 /dev/sda b 8 0
mkdir -p /newroot
for module in ata_piix ext4 ext3 ext2; do modprobe "$module" 2>/dev/null || true; done
mounted=0
for attempt in 1 2 3 4 5 6 7 8 9 10; do
    for filesystem in ext4 ext3 ext2; do
        if mount -n -t "$filesystem" -o rw /dev/sda /newroot 2>/dev/null; then mounted=1; break; fi
    done
    [ "$mounted" = 0 ] || break
    sleep 1
done
if [ "$mounted" = 0 ]; then
    echo 'RESTUNTS_VM_ROOT_DISK_FAILED'
    poweroff -f
    exit 1
fi
mount -n --move /dev /newroot/dev 2>/dev/null || mount -n -o move /dev /newroot/dev || {
    echo 'RESTUNTS_VM_DEV_MOVE_FAILED'
    poweroff -f
    exit 1
}
umount -n /proc
umount -n /sys
exec "$run_init" /newroot /init < /newroot/dev/console > /newroot/dev/console 2>&1
EOF
"${root_command[@]}" rm -f "$work_dir/initramfs/init"
"${root_command[@]}" cp "$work_dir/disk-init" "$work_dir/initramfs/init"
"${root_command[@]}" chmod 755 "$work_dir/initramfs/init"
"${root_command[@]}" mkdir -p "$work_dir/initramfs/dev"
"${root_command[@]}" test -c "$work_dir/initramfs/dev/console" || \
    "${root_command[@]}" mknod -m 600 "$work_dir/initramfs/dev/console" c 5 1
# shellcheck disable=SC2016 # Positional parameters belong to the child shell.
"${root_command[@]}" bash -c 'set -euo pipefail; cd "$1"; find . -print0 | cpio --null -o --format=newc 2> "$2" | gzip -1 > "$3"' \
    bash "$work_dir/initramfs" "$result_dir/cpio.log" "$work_dir/initramfs.gz"
"${root_command[@]}" chmod a+r "$work_dir/initramfs.gz"
sha256sum "$work_dir/vmlinuz" > "$result_dir/kernel.sha256"
sha256sum "$work_dir/original-initrd" > "$result_dir/original-initrd.sha256"
stat -c 'Boot initramfs: %s bytes' "$work_dir/initramfs.gz"
stat -c 'Root filesystem disk: %s bytes' "$work_dir/rootfs.ext3"
if (( $(stat -c %s "$work_dir/initramfs.gz") > 67108864 )); then
    echo 'The boot initramfs exceeds the 64 MiB bound for the historical i386 kernel.' >&2
    exit 1
fi
# The old i386 kernel rejects QEMU's valid ACPI tables when its BIOS date is
# absent. Its own diagnostic requests acpi=force; this also enables S5 shutdown.
kernel_arguments='console=ttyS0 rdinit=/init panic=1'
[[ "$architecture" != i386 ]] || kernel_arguments+=' acpi=force'
timeout --kill-after=10s 600s qemu-system-x86_64 \
    -machine pc -accel tcg -cpu "$cpu" -m 4096 -smp 2 \
    -display none -monitor none -serial "file:$result_dir/serial.log" -no-reboot \
    -netdev user,id=net0 -device e1000,netdev=net0 \
    -drive "file=$work_dir/rootfs.ext3,format=raw,if=ide,index=0" \
    -kernel "$work_dir/vmlinuz" -initrd "$work_dir/initramfs.gz" \
    -append "$kernel_arguments" > "$result_dir/qemu.log" 2>&1 &
qemu_pid=$!
captured=0
while kill -0 "$qemu_pid" 2>/dev/null; do
    if (( !captured )) && [[ -f "$result_dir/serial.log" ]] && \
        grep -q '^RESTUNTS_FUSE_MENU_READY' "$result_dir/serial.log"; then
        cp "$result_dir/Xvfb_screen0" "$result_dir/game-menu.xwd"
        DISPLAY=":$display_number" xwininfo -root -tree > "$result_dir/windows.log"
        grep -q '"restunts" "restunts"' "$result_dir/windows.log"
        captured=1
    fi
    sleep 1
done
qemu_status=0
wait "$qemu_pid" || qemu_status=$?
qemu_pid=
cat "$result_dir/qemu.log"
cat "$result_dir/serial.log"
[[ "$qemu_status" == 0 ]]
grep -q '^RESTUNTS_COMPATIBILITY_PASSED ' "$result_dir/serial.log"
grep -q '^RESTUNTS_VM_FINISHED status=0' "$result_dir/serial.log"
[[ "$captured" == 1 ]]
echo "Ubuntu 10.04 VM compatibility passed. Evidence: $result_dir"
