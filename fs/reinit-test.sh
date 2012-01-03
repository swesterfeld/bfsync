#!/bin/bash
fusermount -u mnt
if test -f mnt/.bfsync/info; then
  echo "umount first"
  exit 1
fi
rm -rf test
bfsync clone master test
bfdbdump init test/bdb
bfsyncfs test mnt
