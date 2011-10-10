AM_CXXFLAGS = $(GLIB_CFLAGS) $(FUSE_CFLAGS) $(SQLITE_LIBS)
AM_CFLAGS = $(GLIB_CFLAGS) $(FUSE_CFLAGS) $(SQLITE_CFLAGS)

bin_PROGRAMS = bfsyncfs
noinst_PROGRAMS = bftesthelper sqlperf

bfsyncfs_SOURCES = bfsyncfs.cc bfsyncserver.cc bfinode.cc bflink.cc bfleakdebugger.cc bfsql.cc
bfsyncfs_LDADD = $(GLIB_LIBS) $(FUSE_LIBS)

sqlperf_SOURCES = sqlperf.cc bfsql.cc
sqlperf_LDADD = $(GLIB_LIBS) $(FUSE_LIBS)
