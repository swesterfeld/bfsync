AM_CXXFLAGS = $(GLIB_CFLAGS) $(FUSE_CFLAGS)
AM_CFLAGS = $(GLIB_CFLAGS) $(FUSE_CFLAGS)

bin_PROGRAMS = bfsyncfs bftesthelper

bfsyncfs_SOURCES = bfsyncfs.cc bfsyncserver.cc bfgitfile.cc
bfsyncfs_LDADD = $(GLIB_LIBS) $(FUSE_LIBS)