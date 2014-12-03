#!/bin/bash
for p in bfsync-clone.1 bfsync-commit.1 bfsync-push.1 bfsync-pull.1 \
         bfsync-get.1 bfsync-put.1 bfsync-check.1 bfsync-log.1 \
         bfsync-gc.1 bfsync-repo-files.1 bfsync-status.1 bfsync-collect.1 \
         bfsync-revert.1 bfsync-continue.1 bfsync-need-continue.1 \
         bfsync-recover.1 bfsync-need-recover.1 bfsync-disk-usage.1 \
         bfsync-new-files.1 bfsync-expire.1 bfsync-upgrade.1 \
         bfsync-need-upgrade.1 bfsync-delete-version.1 \
         bfsync-undelete-version.1 bfsync-config-set.1 \
         bfsync-transfer-bench.1 bfsync-sql-export.1 bfsync-find-missing.1 \
         bfsync-copy-expire.1 bfsync-get-repo-id.1 bfsync-diff.1
do
  a2x -f manpage $p.txt
  asciidoc -f asciidoc.conf -b xhtml11 -d manpage $p.txt
done
exit

# old documentation
for p in bfsync.1 bfsyncfs.1
do \
  curl -sfS http://testbit.eu/$p?action=render >$p.web; \
  curl -sfS "http://testbit.eu/index.php?title=$p&action=raw" > $p.wiki; \
  wikihtml2man.py $p.web >$p; \
  groff -mandoc -Thtml < $p >$p.html; \
  rm $p.web; \
done
