datadir = @datadir@
pkgconfigdir = $(datadir)/pkgconfig
LIB_VERSION = @LIB_VERSION@

lib_LTLIBRARIES = libkwordexp.la libkio.la libkmalloc.la

libkio_la_SOURCES = kio.c
libkmalloc_la_SOURCES = kmalloc.c
libkwordexp_la_SOURCES = kwordexp.c

pkgconfig_DATA = kio.pc kmalloc.pc kwordexp.pc

noinst_HEADERS = kio_internal.h kmalloc_internal.h kwordexp_internal.h

AM_CFLAGS  = -Wall -Wextra -Werror -flto -I./include -I../include
AM_LDFLAGS = -flto -version-info $(LIB_VERSION)

