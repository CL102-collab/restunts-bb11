"""The host ABI shared by AppImage SDK extraction and final closure checks."""

# Keep optional compatibility libraries (notably libnsl.so.1) out of this set:
# modern distributions need not install them alongside glibc. All other direct
# dependencies must be provided inside the AppImage and meet its ABI baseline.
SYSTEM_RUNTIME_LIBRARIES = frozenset({
    "libc.so.6", "libm.so.6", "libdl.so.2", "libpthread.so.0", "librt.so.1", "libresolv.so.2",
    "ld-linux.so.2", "ld-linux-x86-64.so.2",
})
