CXXFLAGS = -Wall `pkg-config fuse glib-2.0 --cflags` -O2
LDFLAGS = `pkg-config fuse glib-2.0 --libs`
OBJS = bfsyncfs.o bfgitfile.o bfsyncserver.o

all: bfsyncfs bfsyncstat

bfsyncfs: $(OBJS)
	$(CXX) -o bfsyncfs $(OBJS) $(LDFLAGS)

clean:
	rm -f bfsyncfs bfsyncstat $(OBJS)
