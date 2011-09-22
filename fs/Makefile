CXXFLAGS = -Wall `pkg-config fuse glib-2.0 --cflags` -O2
LDFLAGS = `pkg-config fuse glib-2.0 --libs`

all: bfsyncfs bfsyncstat

clean:
	rm -f bfsyncfs bfsyncstat
