GC_LIBS = @GC_LIBS@
GC_CFLAGS = @GC_CFLAGS@

noinst_PROGRAMS = runTest

runTest_SOURCES = runTest.c
runTest_CFLAGS = $(GC_CFLAGS)
runTest_LDADD  = $(top_builddir)/src/libkwordexp.la
runTest_LDADD += $(top_builddir)/src/libkio.la
runTest_LDADD += $(top_builddir)/src/libkmalloc.la
runTest_LDADD += $(GC_LIBS) 