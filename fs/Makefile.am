AM_CXXFLAGS = $(GLIB_CFLAGS) $(FUSE_CFLAGS) $(SQLITE_LIBS)
AM_CFLAGS = $(GLIB_CFLAGS) $(FUSE_CFLAGS) $(SQLITE_CFLAGS)

bin_PROGRAMS = bfsyncfs
noinst_PROGRAMS = bftesthelper sqlperf bfperf

lib_LTLIBRARIES = libbfsync.la

libbfsync_la_SOURCES = bfsyncfs.cc bflink.cc bfinode.cc bfleakdebugger.cc bfsql.cc bfsyncserver.cc bfidhash.cc \
                       bfhistory.cc bfcfgparser.cc
libbfsync_la_LIBADD = $(GLIB_LIBS) $(FUSE_LIBS)

bfsyncfs_SOURCES = bfmain.cc
bfsyncfs_LDADD = $(GLIB_LIBS) $(FUSE_LIBS) libbfsync.la

bfperf_SOURCES = bfperf.cc
bfperf_LDADD = $(GLIB_LIBS) $(FUSE_LIBS) libbfsync.la

sqlperf_SOURCES = sqlperf.cc
sqlperf_LDADD = $(GLIB_LIBS) $(FUSE_LIBS) libbfsync.la
