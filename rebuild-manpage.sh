#!/bin/bash
for p in bfsync.1
do \
  curl -sfS http://testbit.eu/$p?action=render >$p.web; \
  curl -sfS "http://testbit.eu/index.php?title=$p&action=raw" > $p.wiki; \
  wikihtml2man.py $p.web >$p; \
  groff -mandoc -Thtml < $p >$p.html; \
  rm $p.web; \
done
