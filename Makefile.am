AUTOMAKE_OPTIONS = dist-bzip2 no-dist-gzip
ACLOCAL_AMFLAGS = -I m4

PYTHON_FILES = setup.py

SUBDIRS = docs fs src
EXTRA_DIST = TODO $(PYTHON_FILES)
DESTDIR = /

install-exec-hook:
	$(PYTHON) setup.py install --root $(DESTDIR) --prefix=${prefix}
