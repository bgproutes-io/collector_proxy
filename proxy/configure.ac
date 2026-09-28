AC_INIT([proxy], [1.0], [contact@bgproutes.io])
AM_INIT_AUTOMAKE([-Wall -Werror foreign subdir-objects])
AC_CONFIG_SRCDIR([proxy.c])

AC_PROG_CC
PKG_PROG_PKG_CONFIG

# Check for dladdr (in libc on some systems, libdl on others)
saved_LIBS=$LIBS
AC_SEARCH_LIBS([dladdr], [dl],
    [AS_CASE([$ac_cv_search_dladdr],
        ["none required"], [DL_LIBS=],
        [DL_LIBS=$ac_cv_search_dladdr])],
    [AC_MSG_ERROR([dladdr not found. Install libdl development files (e.g., libc6-dev)])])
LIBS=$saved_LIBS
AC_SUBST([DL_LIBS])

# Check for required libraries.
PKG_CHECK_MODULES([OPENSSL], [openssl], [],
    [AC_MSG_ERROR([OpenSSL not found. Install the OpenSSL development package (e.g., libssl-dev or openssl-devel)])])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
