#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([kwordexp],[0.0.1],[mako10k@mk10.org])
AC_CONFIG_SRCDIR([src/kwordexp.c])
AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE([foreign])
LT_INIT()

AC_ARG_ENABLE([debug], [AS_HELP_STRING([--enable-debug], [Enable debugging])], [debug=$enableval], [debug=no])
AM_CONDITIONAL([DEBUG], [test "x$debug" = "xyes"])
if test "x$debug" = "xyes"; then
    AC_DEFINE([DEBUG], [1], [Define to 1 if debugging is enabled])
    CFLAGS="${CFLAGS/-O?/}"
    CFLAGS="${CFLAGS/-g?/}"
    CFLAGS="${CFLAGS} -O0 -g3"
fi


# Checks for programs.
AC_PROG_CC

# Checks for libraries.
PKG_CHECK_MODULES([GC], [bdw-gc])

# Checks for header files.
AC_CHECK_HEADERS([stddef.h]) 

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_PID_T

# Checks for library functions.
AC_CHECK_FUNCS([setenv])
AC_CHECK_FUNCS([strtol])
AC_CHECK_FUNCS([dup2])
AC_FUNC_FORK
AC_FUNC_MALLOC
AC_FUNC_REALLOC

AC_SUBST([LIB_VERSION], [1:1:1])

AC_CONFIG_FILES([Makefile src/Makefile include/Makefile test/Makefile])
AC_CONFIG_FILES([src/kio.pc src/kwordexp.pc src/kmalloc.pc])

AC_CONFIG_MACRO_DIRS([m4])

AC_OUTPUT
