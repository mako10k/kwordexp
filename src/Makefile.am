datadir = @datadir@
pkgconfigdir = $(datadir)/pkgconfig
LIB_VERSION = @LIB_VERSION@
GC_CFLAGS = @GC_CFLAGS@
GC_LIBS = @GC_LIBS@

lib_LTLIBRARIES = libkio.la libkmalloc.la libkwordexp.la

libkio_la_SOURCES = kio.c

libkmalloc_la_SOURCES = kmalloc.c
libkmalloc_la_LIBADD = libkio.la

libkwordexp_la_CFLAGS = $(GC_CFLAGS)
libkwordexp_la_SOURCES = kwordexp.c
libkwordexp_la_LIBADD = libkmalloc.la libkio.la $(GC_LIBS)

pkgconfig_DATA = kio.pc kmalloc.pc kwordexp.pc

noinst_HEADERS = kio_internal.h kmalloc_internal.h kwordexp_internal.h

AM_CFLAGS  = -Wall -Wextra -Werror -flto -I./include -I../include
AM_LDFLAGS = -flto -version-info $(LIB_VERSION)

