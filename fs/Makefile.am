AM_CXXFLAGS = $(GLIB_CFLAGS) $(FUSE_CFLAGS) -I/usr/local/BerkeleyDB.5.3/include
AM_LDFLAGS = -L/usr/local/BerkeleyDB.5.3/lib
AM_CFLAGS = $(GLIB_CFLAGS) $(FUSE_CFLAGS)

BDB_LIBS = -ldb_cxx

bin_PROGRAMS = bfsyncfs
noinst_PROGRAMS = bftesthelper bfdbdump bfproftest bflocktest bflockcheck bftestidsort bfperf

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

bfperf_SOURCES = bfperf.cc
bfperf_LDADD = $(GLIB_LIBS) $(FUSE_LIBS) libbfsync.la

bfdbdump_SOURCES = bfdbdump.cc
bfdbdump_LDADD = $(GLIB_LIBS) $(FUSE_LIBS) $(BDB_LIBS) libbfsync.la

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
