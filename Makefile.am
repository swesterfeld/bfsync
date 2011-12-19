AUTOMAKE_OPTIONS = dist-bzip2 no-dist-gzip

SUBDIRS = docs fs
EXTRA_DIST = TODO

install-exec-hook:
	$(PYTHON) setup.py install --prefix=${prefix}
