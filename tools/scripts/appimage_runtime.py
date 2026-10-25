"""The host ABI shared by AppImage SDK extraction and final closure checks."""

# Host GLX/EGL drivers need the X11/XCB core libraries from the same system.
# Bundled historical copies can lack symbols used by modern graphics drivers.
SYSTEM_GRAPHICS_LIBRARIES = frozenset({"libX11.so.6", "libX11-xcb.so.1", "libxcb.so.1"})

# Keep optional compatibility libraries (notably libnsl.so.1) out of this set:
# modern distributions need not install them alongside glibc. Other direct
# dependencies must be provided inside the AppImage and meet its ABI baseline.
SYSTEM_RUNTIME_LIBRARIES = frozenset({
    "libc.so.6", "libm.so.6", "libdl.so.2", "libpthread.so.0", "librt.so.1", "libresolv.so.2",
    "ld-linux.so.2", "ld-linux-x86-64.so.2", "ld-linux-armhf.so.3", "ld-linux-aarch64.so.1",
}) | SYSTEM_GRAPHICS_LIBRARIES


def is_system_graphics_library(name):
    """Recognize core SONAMEs, their versioned files, and linker aliases."""
    for library in SYSTEM_GRAPHICS_LIBRARIES:
        basename = library.partition(".so.")[0] + ".so"
        if name == basename or name.startswith(basename + "."):
            return True
    return False
