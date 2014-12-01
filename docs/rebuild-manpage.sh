#!/bin/bash
for p in bfsync-clone.1 bfsync-commit.1 bfsync-push.1 bfsync-pull.1 \
         bfsync-get.1 bfsync-put.1 bfsync-check.1
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
