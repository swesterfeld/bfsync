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

if [ "x$1" = "xchange2-same" ]; then
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
    echo "edit repo A1" >> f
    bfsync2 commit
    echo "edit repo A2" >> f
    bfsync2 commit
  )
  (
    cd b
    echo "edit repo B1" >> f
    bfsync2 commit
    echo "edit repo B2" >> f
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

if [ "x$1" = "xcreate-indep" ]; then
  # create file-a in repo a
  (
    cd a
    echo "new file a" > file-a
    bfsync2 commit
  )
  # create file-b in repo b
  (
    cd b
    echo "new file b" > file-b
    bfsync2 commit
  )
  # merge
  sync_repos
  echo "#########################################################################"
  echo "after merge:"
  echo "#########################################################################"
  echo "# REPO A:"
  ls -l a
  cat a/file-a
  cat a/file-b
  echo "# root"
  stat a
  echo "# REPO B:"
  ls -l b
  cat b/file-a
  cat b/file-b
  echo "# root"
  stat b
fi

if [ "x$1" = "xhardlink" ]; then
  # create f on both repos
  (
    cd a
    echo "common file" > f
    bfsync2 commit
  )
  sync_repos
  # create hardlink in both repos
  (
    cd a
    ln f af
    bfsync2 commit
  )
  (
    cd b
    ln f bf
    bfsync2 commit
  )
  # merge
  sync_repos
  echo "#########################################################################"
  echo "after merge:"
  echo "#########################################################################"
  echo "# REPO A:"
  stat a/f
  stat a/af
  echo "# REPO B:"
  stat b/f
  stat b/bf
fi


if [ "x$1" = "x" ]; then
  echo
  echo "Supported merge tests:"
  echo " - change-same   -> edit contents of same file on repo a & b"
  echo " - change2-same  -> edit contents of same file on repo a & b, two edits for each repo"
  echo " - create-same   -> independently create file with same name in repo a & b"
  echo " - create-indep  -> create independent file-a in repo a and file-b in repo-b"
  echo " - hardlink      -> create indepentent hardlinks on the same inode"
fi
