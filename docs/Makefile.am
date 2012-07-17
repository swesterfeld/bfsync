dist_man_MANS = bfsync.1 bfsyncfs.1

EXTRA_DIST = bfsync.1.wiki bfsyncfs.1.wiki bfsync-overview.odg rebuild-manpage.sh

DOCS_DIR = ~/public_html/bfsync/docs-$(VERSION)

publish-docs:
	mkdir -p $(DOCS_DIR)
	for i in $(dist_man_MANS); do \
	  cp $$i.html $(DOCS_DIR); \
	done
	cd $(DOCS_DIR) && find . -type f -print0 | xargs -0 git add
