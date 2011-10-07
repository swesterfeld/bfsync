AM_CXXFLAGS = $(GLIB_CFLAGS) $(FUSE_CFLAGS) $(SQLITE_LIBS)
AM_CFLAGS = $(GLIB_CFLAGS) $(FUSE_CFLAGS) $(SQLITE_CFLAGS)

bin_PROGRAMS = bfsyncfs bftesthelper

bfsyncfs_SOURCES = bfsyncfs.cc bfsyncserver.cc bfgitfile.cc bfinode.cc
bfsyncfs_LDADD = $(GLIB_LIBS) $(FUSE_LIBS)
