AC_INIT([bfsync], [0.2.0])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([fs/bfsyncfs.cc])
AC_CONFIG_HEADER([config.h])
AC_PROG_CXX
AC_PROG_LIBTOOL
AC_PATH_PROG(PYTHON, python)

dnl
dnl pkg-config
dnl
AC_DEFUN([AC_PKG_CONFIG_REQUIREMENTS],
[
    AC_CHECK_PROG(HAVE_PKG_CONFIG,pkg-config,yes,no)
    if test "$HAVE_PKG_CONFIG" = "no"; then
      AC_MSG_ERROR([You need to have pkg-config installed to build this package.

Debian users: aptitude install pkg-config
])
    fi
])

dnl
dnl glib
dnl
AC_DEFUN([AC_GLIB_REQUIREMENTS],
[
    PKG_CHECK_MODULES(GLIB, glib-2.0)
    AC_SUBST(GLIB_CFLAGS)
    AC_SUBST(GLIB_LIBS)
])

dnl
dnl fuse
dnl
AC_DEFUN([AC_FUSE_REQUIREMENTS],
[
    PKG_CHECK_MODULES(FUSE, fuse)
    AC_SUBST(FUSE_CFLAGS)
    AC_SUBST(FUSE_LIBS)
])

dnl ----------------------------- from acbirnet.m4 -------------------------------
dnl Check whether cc accepts a certain option
dnl MC_PROG_CC_SUPPORTS_OPTION(OPTIONS, ACTION-IF-FOUND [,ACTION-IF-NOT-FOUND])
AC_DEFUN([MC_PROG_CC_SUPPORTS_OPTION], [
AC_MSG_CHECKING([whether ${CC-cc} supports $1])
echo >conftest.c;
if ${CC-cc} [$1] -c $CFLAGS conftest.c >/dev/null 2>&1 ; then
    AC_MSG_RESULT(yes)
    [$2]
else
    AC_MSG_RESULT(no)
    [$3]
fi
rm -fr conftest*
])dnl

# Check requirement sets

AC_PKG_CONFIG_REQUIREMENTS
AC_GLIB_REQUIREMENTS
AC_FUSE_REQUIREMENTS

# Less cluttered build output
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

MC_PROG_CC_SUPPORTS_OPTION([-Wall], [
  CFLAGS="$CFLAGS -Wall"
  CXXFLAGS="$CXXFLAGS -Wall"
])


AC_CONFIG_FILES([
  Makefile
  fs/Makefile
  docs/Makefile
  src/Makefile
  src/bfsync/Makefile
])
AC_OUTPUT
