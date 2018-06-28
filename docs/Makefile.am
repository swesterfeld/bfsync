DOCS_TXT = bfsync.1.txt bfsyncfs.1.txt bfsync-*.1.txt
DOCS_MAN = bfsync.1     bfsyncfs.1     bfsync-*.1

dist_man_MANS = $(DOCS_MAN)

EXTRA_DIST = $(DOCS_MAN) $(DOCS_TXT) bfsync.1.wiki bfsyncfs.1.wiki bfsync-overview.odg rebuild-manpage.sh \
	     command-list.txt asciidoc.conf cmd-list.py

DOCS_DIR = ~/public_html/bfsync/docs-$(VERSION)
PROJECT_DIR = ~/public_html/bfsync

publish-docs:
	mkdir -p $(DOCS_DIR)
	for i in $(dist_man_MANS); do \
	  cp $$i.html $(DOCS_DIR); \
	done
	cd $(DOCS_DIR) && find . -type f -print0 | xargs -0 git add
	cp ../NEWS $(PROJECT_DIR)
	cd $(PROJECT_DIR) && git add NEWS
