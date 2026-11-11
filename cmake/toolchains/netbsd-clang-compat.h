/* NetBSD headers expect GCC builtins that Clang does not provide.
 * Recover them from Clang's target-specific limits, widths and signedness.
 */
#ifndef RESTUNTS_NETBSD_CLANG_COMPAT_H
#define RESTUNTS_NETBSD_CLANG_COMPAT_H

#ifndef __WCHAR_MIN__
#ifdef __WCHAR_UNSIGNED__
#define __WCHAR_MIN__ 0
#else
#define __WCHAR_MIN__ (-__WCHAR_MAX__ - 1)
#endif
#endif

#ifndef __WINT_MIN__
#ifdef __WINT_UNSIGNED__
#define __WINT_MIN__ 0
#else
#define __WINT_MIN__ (-__WINT_MAX__ - 1)
#endif
#endif

#ifndef __SIG_ATOMIC_TYPE__
/* Both supported NetBSD ABIs use an int-width signal type. The maximum's
 * literal suffix preserves its signedness without assuming a fixed width.
 */
#if __SIG_ATOMIC_WIDTH__ != __INT_WIDTH__
#error Unsupported NetBSD signal atomic width
#endif
#define __SIG_ATOMIC_TYPE__ __typeof__(__SIG_ATOMIC_MAX__)
#endif

#endif
