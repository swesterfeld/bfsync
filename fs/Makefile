CXXFLAGS = -Wall `pkg-config fuse glib-2.0 --cflags` -O2
LDFLAGS = `pkg-config fuse glib-2.0 --libs`
OBJS = bfsyncfs.o bfgitfile.o

all: bfsyncfs bfsyncstat

bfsyncfs: bfsyncfs.o bfgitfile.o
	$(CXX) -o bfsyncfs $(OBJS) $(LDFLAGS)

clean:
	rm -f bfsyncfs bfsyncstat $(OBJS)
