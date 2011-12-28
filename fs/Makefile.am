AM_CXXFLAGS = $(GLIB_CFLAGS) $(FUSE_CFLAGS) $(SQLITE_LIBS) -I/usr/local/BerkeleyDB.5.3/include
AM_LDFLAGS = -L/usr/local/BerkeleyDB.5.3/lib
AM_CFLAGS = $(GLIB_CFLAGS) $(FUSE_CFLAGS) $(SQLITE_CFLAGS)

BDB_LIBS = -ldb_cxx

bin_PROGRAMS = bfsyncfs
noinst_PROGRAMS = bftesthelper sqlperf bfperf

lib_LTLIBRARIES = libbfsync.la

BFSYNC_HDRS = bfinode.hh bfsql.hh bfidhash.hh bfsyncfs.hh bflink.hh bfsyncserver.hh bfhistory.hh \
              bfcfgparser.hh bfleakdebugger.hh bfbdb.hh

libbfsync_la_SOURCES = bfsyncfs.cc bflink.cc bfinode.cc bfleakdebugger.cc bfsql.cc bfsyncserver.cc bfidhash.cc \
                       bfhistory.cc bfcfgparser.cc bfbdb.cc $(BFSYNC_HDRS)
libbfsync_la_LIBADD = $(GLIB_LIBS) $(FUSE_LIBS) $(BDB_LIBS)

bfsyncfs_SOURCES = bfmain.cc
bfsyncfs_LDADD = $(GLIB_LIBS) $(FUSE_LIBS) libbfsync.la

bfperf_SOURCES = bfperf.cc
bfperf_LDADD = $(GLIB_LIBS) $(FUSE_LIBS) libbfsync.la

sqlperf_SOURCES = sqlperf.cc
sqlperf_LDADD = $(GLIB_LIBS) $(FUSE_LIBS) libbfsync.la

bftesthelper_SOURCES = bftesthelper.cc
