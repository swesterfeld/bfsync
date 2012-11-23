AM_CXXFLAGS = $(GLIB_CFLAGS) $(FUSE_CFLAGS)
AM_CFLAGS = $(GLIB_CFLAGS) $(FUSE_CFLAGS)

DESTDIR = /

BDB_LIBS = -ldb_cxx

bin_PROGRAMS = bfsyncfs bfsyncca
noinst_PROGRAMS = bftesthelper bfdbdump bfproftest bflocktest bflockcheck bftestidsort bfperf bfwalktest bfdefrag bfrandomize

lib_LTLIBRARIES = libbfsync.la

PYTHON_FILES = setup.py
SWIG_FILES = bfsyncdb.i
EXTENSION_FILES = bfsyncdb.cc bfsyncdb.hh

EXTRA_DIST = $(PYTHON_FILES) $(SWIG_FILES) $(EXTENSION_FILES)

BFSYNC_HDRS = bfinode.hh bfidhash.hh bfsyncfs.hh bflink.hh bfsyncserver.hh bfhistory.hh \
              bfcfgparser.hh bfleakdebugger.hh bfbdb.hh bftimeprof.hh bfidsorter.hh \
              bfdeduptable.hh

libbfsync_la_SOURCES = bfsyncfs.cc bflink.cc bfinode.cc bfleakdebugger.cc bfsyncserver.cc bfidhash.cc \
                       bfhistory.cc bfcfgparser.cc bfbdb.cc bftimeprof.cc $(BFSYNC_HDRS)
libbfsync_la_LIBADD = $(GLIB_LIBS) $(FUSE_LIBS) $(BDB_LIBS)

bfsyncfs_SOURCES = bfmain.cc
bfsyncfs_LDADD = $(GLIB_LIBS) $(FUSE_LIBS) libbfsync.la

bfsyncca_SOURCES = bfsyncca.cc
bfsyncca_LDADD = $(GLIB_LIBS)

bfdefrag_SOURCES = bfdefrag.cc
bfdefrag_LDADD = $(GLIB_LIBS) $(FUSE_LIBS) libbfsync.la

bfrandomize_SOURCES = bfrandomize.cc
bfrandomize_LDADD = $(GLIB_LIBS) $(FUSE_LIBS) libbfsync.la

bfperf_SOURCES = bfperf.cc
bfperf_LDADD = $(GLIB_LIBS) $(FUSE_LIBS) libbfsync.la

bfdbdump_SOURCES = bfdbdump.cc
bfdbdump_LDADD = $(GLIB_LIBS) $(FUSE_LIBS) $(BDB_LIBS) libbfsync.la

bfwalktest_SOURCES = bfwalktest.cc bfsyncdb.cc
bfwalktest_LDADD = $(GLIB_LIBS) $(FUSE_LIBS) $(BDB_LIBS) libbfsync.la

bfproftest_SOURCES = bfproftest.cc
bfproftest_LDADD = $(GLIB_LIBS) $(FUSE_LIBS) $(BDB_LIBS) libbfsync.la

bflocktest_SOURCES = bflocktest.cc
bflocktest_LDADD = $(GLIB_LIBS) $(FUSE_LIBS) $(BDB_LIBS) libbfsync.la

bflockcheck_SOURCES = bflockcheck.cc
bflockcheck_LDADD = $(GLIB_LIBS) $(FUSE_LIBS) $(BDB_LIBS) libbfsync.la

bftestidsort_SOURCES = bftestidsort.cc
bftestidsort_LDADD = $(GLIB_LIBS) $(FUSE_LIBS) $(BDB_LIBS) libbfsync.la

bftesthelper_SOURCES = bftesthelper.cc

all-local: setup.py
	$(PYTHON) setup.py build

clean-local:
	$(PYTHON) setup.py clean --all

install-exec-hook: setup.py
	$(PYTHON) setup.py install --root $(DESTDIR) --prefix=${prefix}
# update ldconfig cache to be able to load python module linked to libbfsync.so
# don't bother if this fails (maybe installing as user into user defined prefix)
#
# I think libtool should do this, but on my test system it doesn't
	/sbin/ldconfig ${libdir} || true
