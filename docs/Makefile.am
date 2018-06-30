DOCS_TXT = bfsync.txt bfsyncfs.txt bfsync-*.txt
DOCS_MAN = bfsync.1   bfsyncfs.1   bfsync-*.1
DOCS_PHP = bfsync.php bfsyncfs.php bfsync-*.php

dist_man_MANS = $(DOCS_MAN)

EXTRA_DIST = $(DOCS_MAN) $(DOCS_TXT) bfsync.1.wiki bfsyncfs.1.wiki bfsync-overview.odg rebuild-manpage.sh \
	     command-list.txt asciidoc.conf cmd-list.py

DOCS_DIR = ~/public_html/bfsync/docs-$(VERSION)
PROJECT_DIR = ~/public_html/bfsync

publish-docs:
	mkdir -p $(DOCS_DIR)
	for i in $(DOCS_PHP); do \
	  cp $$i $(DOCS_DIR); \
	done
	cd $(DOCS_DIR) && find . -type f -print0 | xargs -0 git add
	cp ../NEWS $(PROJECT_DIR)
	cd $(PROJECT_DIR) && git add NEWS
