#!/bin/bash

if test -d merge-test; then
  fusermount -u merge-test/a
  fusermount -u merge-test/b
  rm -rf merge-test
fi

mkdir merge-test
cd merge-test
bfsync2 init master
bfsync2 clone master repo-a
bfsync2 clone master repo-b
echo 'default { get "'$PWD/repo-b'"; }' >> repo-a/.bfsync/config
echo 'default { get "'$PWD/repo-a'"; }' >> repo-b/.bfsync/config
mkdir a
mkdir b
bfsyncfs repo-a a
bfsyncfs repo-b b

sync_repos()
{
  # send changes from a to master
  (
    cd a
    bfsync2 push
  )
  # merge changes from master into b; send merged result to master
  (
    cd b
    bfsync2 pull
    bfsync2 push
    bfsync2 get # get missing file contents
  )
  # pull merged changes into repo a
  (
    cd a
    bfsync2 pull
    bfsync2 get # get missing file contents
  )
}

if [ "x$1" = "xcreate-same" ]; then
  (
    echo "### CREATE x ON REPO a"
    cd a
    echo "Hello Repo A" > x
    bfsync2 commit
  )
  (
    cd b
    echo "### CREATE x ON REPO b"
    echo "Hello Repo B" > x
    bfsync2 commit
  )
  sync_repos
fi

if [ "x$1" = "xchange-same" ]; then
  # create f on both repos
  (
    cd a
    echo "common file" > f
    bfsync2 commit
  )
  sync_repos
  # edit f on both repos
  (
    cd a
    echo "edit repo A" >> f
    bfsync2 commit
  )
  (
    cd b
    echo "edit repo B" >> f
    bfsync2 commit
  )
  # merge
  sync_repos
  echo "#########################################################################"
  echo "after merge:"
  echo "#########################################################################"
  echo "# REPO A:"
  cat a/f
  echo "# REPO B:"
  cat b/f
fi
