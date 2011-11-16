#!/usr/bin/python

import os
import sys

if os.path.exists ("merge-test"):
  os.system ("fusermount -u merge-test/a")
  os.system ("fusermount -u merge-test/b")
  os.system ("rm -rf merge-test")

os.mkdir ("merge-test")
os.chdir ("merge-test")
os.system ("bfsync2 init master")
os.system ("bfsync2 clone master repo-a")
os.system ("bfsync2 clone master repo-b")
os.system ("""echo 'default { get "'$PWD/repo-b'"; }' >> repo-a/.bfsync/config""")
os.system ("""echo 'default { get "'$PWD/repo-a'"; }' >> repo-b/.bfsync/config""")
os.mkdir ("a")
os.mkdir ("b")
os.system ("bfsyncfs repo-a a")
os.system ("bfsyncfs repo-b b")

tests = []

class Repo:
  def __init__ (self, path):
    self.path = path

  def run (self, cmd):
    old_cwd = os.getcwd()
    os.chdir (self.path)
    os.system (cmd)
    os.chdir (old_cwd)

def run_b (cmd):
  old_cwd = os.getcwd()
  os.chdir ("b")
  os.system (cmd)
  os.chdir (old_cwd)

def sync_repos (a, b):
  # send changes from a to master
  a.run ("bfsync2 push")
  # merge changes from master into b; send merged result to master
  b.run ("bfsync2 pull")
  b.run ("bfsync2 push")
  b.run ("bfsync2 get")   # get missing file contents
  # pull merged changes into repo a
  a.run ("bfsync2 pull")
  a.run ("bfsync2 get")   # get missing file contents

def create_same (a, b):
  a.run ("echo 'Hello Repo A' > x")
  a.run ("bfsync2 commit")
  b.run ("echo 'Hello Repo B' > x")
  b.run ("bfsync2 commit")
  sync_repos (a, b)

tests += [
  ( create_same, "create-same", "independently create file with same name in repo a & b" )
]

def change_same (a, b):
  # create f on both repos
  a.run ("echo 'common file' > f")
  a.run ("bfsync2 commit")
  sync_repos (a, b)
  # edit f on both repos
  a.run ("echo 'edit repo A' >> f")
  a.run ("bfsync2 commit")
  b.run ("echo 'edit repo B' >> f")
  b.run ("bfsync2 commit")
  # merge
  sync_repos (a, b)
  print "#########################################################################"
  print "after merge:"
  print "#########################################################################"
  print "# REPO A:"
  a.run ("cat f")
  print "# REPO B:"
  b.run ("cat f")

tests += [
  ( change_same, "change-same", "edit contents of same file on repo a & b" )
]

def change2_same (a, b):
  # create f on both repos
  a.run ("echo 'common file' > f")
  a.run ("bfsync2 commit")
  sync_repos (a, b)
  # edit f on both repos
  a.run ("echo 'edit repo A1' >> f")
  a.run ("bfsync2 commit")
  a.run ("echo 'edit repo A2' >> f")
  a.run ("bfsync2 commit")
  b.run ("echo 'edit repo B1' >> f")
  b.run ("bfsync2 commit")
  b.run ("echo 'edit repo B2' >> f")
  b.run ("bfsync2 commit")
  # merge
  sync_repos (a, b)
  print "#########################################################################"
  print "after merge:"
  print "#########################################################################"
  print "# REPO A:"
  a.run ("cat f")
  print "# REPO B:"
  b.run ("cat f")

tests += [
  ( change2_same, "change2-same", "edit contents of same file on repo a & b, two edits for each repo" )
]

def create_indep (a, b):
  # create file-a in repo a
  a.run ("echo 'new file a' > file-a")
  a.run ("bfsync2 commit")
  # create file-b in repo b
  b.run ("echo 'new file b' > file-b")
  b.run ("bfsync2 commit")
  # merge
  sync_repos (a, b)
  print "#########################################################################"
  print "after merge:"
  print "#########################################################################"
  print "# REPO A:"
  a.run ("ls -l")
  a.run ("cat file-a")
  a.run ("cat file-b")
  print "# root"
  a.run ("stat .")
  print "# REPO B:"
  b.run ("ls -l")
  b.run ("cat file-a")
  b.run ("cat file-b")
  print "# root"
  b.run ("stat .")

tests += [
  ( create_indep, "create-indep", "create independent file-a in repo a and file-b in repo-b" )
]

def hardlink (a, b):
  # create f on both repos
  a.run ("echo 'common file' > f")
  a.run ("bfsync2 commit")
  sync_repos(a, b)

  # create hardlink in both repos
  a.run ("ln f af")
  a.run ("bfsync2 commit")
  b.run ("ln f bf")
  b.run ("bfsync2 commit")

  # merge
  sync_repos (a, b)
  print "#########################################################################"
  print "after merge:"
  print "#########################################################################"
  print "# REPO A:"
  a.run ("stat f")
  a.run ("stat af")
  print "# REPO B:"
  b.run ("stat f")
  b.run ("stat bf")

tests += [
  ( hardlink, "hardlink", "create independent hardlinks on the same inode" )
]

def hardlink_rm (a, b):
  # create f on both repos
  a.run ("echo 'common file' > f")
  a.run ("ln f fxa")
  a.run ("ln f fxb")
  a.run ("bfsync2 commit")

  sync_repos (a, b)
  # remove one hardlink in both repos
  a.run ("rm fxa")
  a.run ("bfsync2 commit")
  b.run ("rm fxb")
  b.run ("bfsync2 commit")

  # merge
  sync_repos (a, b)
  print "#########################################################################"
  print "after merge:"
  print "#########################################################################"
  print "# REPO A:"
  a.run ("stat f")
  print "# REPO B:"
  b.run ("stat f")

tests += [
  ( hardlink_rm, "hardlink-rm", "delete independent hardlinks on the same inode" )
]

def rm_change (x, y, a, b):
  # create f on both repos
  x.run ("echo 'common file' > f")
  x.run ("bfsync2 commit")
  sync_repos (x, y)
  # update f in x
  x.run ("echo 'updated file'  > f")
  x.run ("bfsync2 commit")
  # rm f in y
  y.run ("rm f")
  y.run ("bfsync2 commit")
  # merge
  sync_repos (a, b)
  print "#########################################################################"
  print "after merge:"
  print "#########################################################################"
  print "# REPO A:"
  a.run ("cat f")
  print "# REPO B:"
  b.run ("cat f")

def rm_change_a (a, b):
  rm_change (a, b, a, b)

tests += [
  ( rm_change_a, "rm-change-a", "change content of file in repo a while deleting it in repo b" )
]

def rm_change_b (a, b):
  rm_change (b, a, a, b)

tests += [
  ( rm_change_b, "rm-change-b", "change content of file in repo b while deleting it in repo a" )
]

def attr_change (a, b):
  a.run ("echo 'common file' > f")
  a.run ("bfsync2 commit")
  sync_repos (a, b)
  # change attributes
  a.run ("chmod 600 f")
  a.run ("bfsync2 commit")
  b.run ("touch f")
  b.run ("bfsync2 commit")
  # merge
  sync_repos (a, b)
  print "#########################################################################"
  print "after merge:"
  print "#########################################################################"
  print "# REPO A:"
  a.run ("stat f")
  print "# REPO B:"
  b.run ("stat f")

tests += [
  ( attr_change, "attr-change", "change attributes of file in repo a & b" )
]

def rm_combine (a, b):
  # create f and g on both repos
  a.run ("echo 'common file' > f")
  a.run ("ln f g")
  a.run ("bfsync2 commit")
  sync_repos (a, b)
  # remove one hardlink in repo a...
  a.run ("rm f")
  a.run ("bfsync2 commit")
  # ... and the other in repo b
  b.run ("rm g")
  b.run ("bfsync2 commit")
  # merge
  sync_repos (a, b)
  print "#########################################################################"
  print "after merge:"
  print "#########################################################################"
  print "# REPO A:"
  a.run ("cat f")
  a.run ("cat g")
  print "# REPO B:"
  b.run ("cat f")
  b.run ("cat g")

tests += [
  ( rm_combine, "rm-combine", "rm links in repo a & b so that the combination removes the inode")
]

def rm_same (a, b):
  # create f in both repos
  a.run ("echo 'common file' > f")
  a.run ("bfsync2 commit")
  sync_repos (a, b)
  # remove f in repo a...
  a.run ("rm f")
  a.run ("bfsync2 commit")
  # ... and in repo b
  b.run ("rm f")
  b.run ("bfsync2 commit")
  # merge
  sync_repos (a, b)
  print "#########################################################################"
  print "after merge:"
  print "#########################################################################"
  print "# REPO A:"
  a.run ("cat f")
  print "# REPO B:"
  b.run ("cat f")

tests += [
  ( rm_same, "rm-same", "rm same file independently in repo a & b")
]

if len (sys.argv) == 2:
  a = Repo ("a")
  b = Repo ("b")
  for t in tests:
    if sys.argv[1] == t[1]:
      print "==================================================================="
      print "Running test: %s\n -> %s" % (t[1], t[2])
      print "==================================================================="
      t[0] (a, b)
      sys.exit (0)

print
print "Supported merge tests:"
for t in tests:
  print " - %-13s -> %s" % (t[1], t[2])
