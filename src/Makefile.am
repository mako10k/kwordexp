datadir = @datadir@
pkgconfigdir = $(datadir)/pkgconfig
LIB_VERSION = @LIB_VERSION@

lib_LTLIBRARIES = libkwordexp.la libkio.la

libkwordexp_la_SOURCES = kwordexp.c
libkio_la_SOURCES = kio.c
noinst_HEADERS = kwordexp_internal.h kio_internal.h

pkgconfig_DATA = kio.pc kwordexp.pc

AM_CFLAGS = -Wall -Wextra -Werror -flto
AM_LDFLAGS = -flto -version-info $(LIB_VERSION)