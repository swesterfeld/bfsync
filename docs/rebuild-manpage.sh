#!/bin/bash

PAGES="$@"
if test -z "$PAGES"; then
  PAGES=`awk < command-list.txt '{print $1".1"}'`
  PAGES="$PAGES bfsync.1 bfsyncfs.1"
fi

cmd-list.py # generate command overview

for p in $PAGES
do
  echo "processing $p"
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
