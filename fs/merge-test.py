#!/usr/bin/python

import os
import sys
import time
from commitutils import commit
from transferutils import get, push, pull
from utils import connect_db

tests = []

class Repo:
  def __init__ (self, path, merge_mode):
    self.path = path
    self.merge_mode = merge_mode

    # connect to db
    self.repo = connect_db (self.path)

  def run (self, cmd):
    old_cwd = os.getcwd()
    os.chdir (self.path)
    os.system (cmd)
    os.chdir (old_cwd)

  def runx (self, cmd):
    old_cwd = os.getcwd()
    os.chdir (self.path)
    if os.system (cmd) != 0:
      raise Exception ("Command %s failed" % cmd)
    os.chdir (old_cwd)

  def commit (self):
    old_cwd = os.getcwd()
    os.chdir (self.path)
    try:
      commit (self.repo)
    except Exception, e:
      print "COMMIT FAILED: %s" % e
      sys.exit (1)
    os.chdir (old_cwd)

  def get (self):
    old_cwd = os.getcwd()
    os.chdir (self.repo.path)
    try:
      get (self.repo, [])
    except Exception, e:
      print "GET FAILED: %s" % e
      sys.exit (1)
    os.chdir (old_cwd)

  def push (self):
    old_cwd = os.getcwd()
    os.chdir (self.repo.path)
    try:
      push (self.repo, [])
    except Exception, e:
      print "PUSH FAILED: %s" % e
      sys.exit (1)
    os.chdir (old_cwd)

  def pull (self, args):
    old_cwd = os.getcwd()
    os.chdir (self.repo.path)
    pull (self.repo, args)
    os.chdir (old_cwd)

  def close (self):
    self.repo.conn.close()
    self.repo = None

def sync_repos (a, b):
  # send changes from a to master
  a.push()
  # merge changes from master into b; send merged result to master
  if b.merge_mode == "m":
    b.pull (["--always-master"])
  elif b.merge_mode == "l":
    b.pull (["--always-local"])
  else:
    b.pull ([])           # interactive
  b.push()
  b.get()                 # get missing file contents
  # pull merged changes into repo a
  a.pull ([])
  a.get()                 # get missing file contents

def create_same (a, b):
  a.runx ("echo 'Hello Repo A' > x")
  a.commit()
  b.runx ("echo 'Hello Repo B' > x")
  b.commit()
  sync_repos (a, b)

tests += [
  ( create_same, "create-same", "independently create file with same name in repo a & b" )
]

def change_same (a, b):
  # create f on both repos
  a.runx ("echo 'common file' > f")
  a.commit()
  sync_repos (a, b)
  # edit f on both repos
  a.runx ("echo 'edit repo A' >> f")
  a.commit()
  b.runx ("echo 'edit repo B' >> f")
  b.commit()
  # merge
  sync_repos (a, b)
  print "#########################################################################"
  print "after merge:"
  print "#########################################################################"
  print "# REPO A:"
  a.runx ("cat f")
  print "# REPO B:"
  b.runx ("cat f")

tests += [
  ( change_same, "change-same", "edit contents of same file on repo a & b" )
]

def change2_same (a, b):
  # create f on both repos
  a.runx ("echo 'common file' > f")
  a.commit()
  sync_repos (a, b)
  # edit f on both repos
  a.runx ("echo 'edit repo A1' >> f")
  a.commit()
  a.runx ("echo 'edit repo A2' >> f")
  a.commit()
  b.runx ("echo 'edit repo B1' >> f")
  b.commit()
  b.runx ("echo 'edit repo B2' >> f")
  b.commit()
  # merge
  sync_repos (a, b)
  print "#########################################################################"
  print "after merge:"
  print "#########################################################################"
  print "# REPO A:"
  a.runx ("cat f")
  print "# REPO B:"
  b.runx ("cat f")

tests += [
  ( change2_same, "change2-same", "edit contents of same file on repo a & b, two edits for each repo" )
]

def create_indep (a, b):
  # create file-a in repo a
  a.runx ("echo 'new file a' > file-a")
  a.commit()
  # create file-b in repo b
  b.runx ("echo 'new file b' > file-b")
  b.commit()
  # merge
  sync_repos (a, b)
  print "#########################################################################"
  print "after merge:"
  print "#########################################################################"
  print "# REPO A:"
  a.runx ("ls -l")
  a.runx ("cat file-a")
  a.runx ("cat file-b")
  print "# root"
  a.runx ("stat .")
  print "# REPO B:"
  b.runx ("ls -l")
  b.runx ("cat file-a")
  b.runx ("cat file-b")
  print "# root"
  b.runx ("stat .")

tests += [
  ( create_indep, "create-indep", "create independent file-a in repo a and file-b in repo-b" )
]

def hardlink (a, b):
  # create f on both repos
  a.runx ("echo 'common file' > f")
  a.commit()
  sync_repos(a, b)

  # create hardlink in both repos
  a.runx ("ln f af")
  a.commit()
  b.runx ("ln f bf")
  b.commit()

  # merge
  sync_repos (a, b)
  print "#########################################################################"
  print "after merge:"
  print "#########################################################################"
  print "# REPO A:"
  a.runx ("stat f")
  a.runx ("stat af")
  print "# REPO B:"
  b.runx ("stat f")
  b.runx ("stat bf")

tests += [
  ( hardlink, "hardlink", "create independent hardlinks on the same inode" )
]

def hardlink_rm (a, b):
  # create f on both repos
  a.runx ("echo 'common file' > f")
  a.runx ("ln f fxa")
  a.runx ("ln f fxb")
  a.commit()

  sync_repos (a, b)
  # remove one hardlink in both repos
  a.runx ("rm fxa")
  a.commit()
  b.runx ("rm fxb")
  b.commit()

  # merge
  sync_repos (a, b)
  print "#########################################################################"
  print "after merge:"
  print "#########################################################################"
  print "# REPO A:"
  a.runx ("stat f")
  print "# REPO B:"
  b.runx ("stat f")

tests += [
  ( hardlink_rm, "hardlink-rm", "delete independent hardlinks on the same inode" )
]

def rm_change (x, y, a, b):
  # create f on both repos
  x.runx ("echo 'common file' > f")
  x.commit()
  sync_repos (x, y)
  # update f in x
  x.runx ("echo 'updated file'  > f")
  x.commit()
  # rm f in y
  y.runx ("rm f")
  y.commit()
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
  a.runx ("echo 'common file' > f")
  a.commit()
  sync_repos (a, b)
  # change attributes
  a.run ("chmod 600 f")
  a.commit()
  b.runx ("touch f")
  b.commit()
  # merge
  sync_repos (a, b)
  print "#########################################################################"
  print "after merge:"
  print "#########################################################################"
  print "# REPO A:"
  a.runx ("stat f")
  print "# REPO B:"
  b.runx ("stat f")

tests += [
  ( attr_change, "attr-change", "change attributes of file in repo a & b" )
]

def rm_combine (a, b):
  # create f and g on both repos
  a.runx ("echo 'common file' > f")
  a.runx ("ln f g")
  a.commit()
  sync_repos (a, b)
  # remove one hardlink in repo a...
  a.runx ("rm f")
  a.commit()
  # ... and the other in repo b
  b.runx ("rm g")
  b.commit()
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
  a.runx ("echo 'common file' > f")
  a.commit()
  sync_repos (a, b)
  # remove f in repo a...
  a.runx ("rm f")
  a.commit()
  # ... and in repo b
  b.runx ("rm f")
  b.commit()
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

def link_coll (a, b):
  # create f in both repos
  a.runx ("echo 'file f' > f")
  a.runx ("echo 'file g' > g")
  a.commit()
  sync_repos (a, b)
  # link f to x in repo a...
  a.runx ("ln f x")
  a.commit()
  # ... and g to x repo b
  b.runx ("ln g x")
  b.commit()
  # merge
  sync_repos (a, b)
  print "#########################################################################"
  print "after merge:"
  print "#########################################################################"
  print "# REPO A:"
  a.runx ("ls -l")
  a.runx ("cat x")
  print "# REPO B:"
  b.runx ("ls -l")
  b.runx ("cat x")

tests += [
  ( link_coll, "link-coll", "create hardlink to x with different target in both repos")
]

def setup_initial():
  if os.path.exists ("merge-test"):
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
  os.system ("rsync -a master repo-a repo-b backup")

def setup():
  os.system ("fusermount -u merge-test/a")
  os.system ("fusermount -u merge-test/b")

  if not os.path.exists ("merge-test/backup"):
    setup_initial()

  os.chdir ("merge-test")

  # rsync'ing the repo data is faster than creating it from scratch
  os.system ("rsync -a --delete backup/* .")

  os.system ("bfsyncfs repo-a a")
  os.system ("bfsyncfs repo-b b")

def main():
  if len (sys.argv) == 2:
    if sys.argv[1] == "setup":
      setup()
    elif sys.argv[1] == "all":
      old_cwd = os.getcwd()
      results = []
      for merge_mode in [ "m", "l" ]:
        for t in tests:
          setup()
          a = Repo ("a", merge_mode)
          b = Repo ("b", merge_mode)
          print "==================================================================="
          print "Running test: %s\n -> %s" % (t[1], t[2])
          print "==================================================================="
          try:
            t[0] (a, b)
          except Exception, e:
            results += [ (t[1] + " / merge=%s" % merge_mode, "FAIL", "%s" % e) ]
          else:
            results += [ (t[1] + " / merge=%s" % merge_mode, "OK") ]
          a.close()
          b.close()
          os.chdir (old_cwd)

      print "==================================================================="
      for result in results:
        print "%30s   %s" % (result[0], result[1])
        if len (result) > 2:
          print "  <=>  %s" % result[2]
    else:
      setup()
      a = Repo ("a", None)
      b = Repo ("b", None)
      for t in tests:
        if sys.argv[1] == t[1]:
          setup()
          print "==================================================================="
          print "Running test: %s\n -> %s" % (t[1], t[2])
          print "==================================================================="
          t[0] (a, b)
    sys.exit (0)
  print
  print "Supported merge tests:"
  for t in tests:
    print " - %-13s -> %s" % (t[1], t[2])

  print " - %-13s -> %s" % ("all", "run all tests")
  print " - %-13s -> %s" % ("setup", "only setup repos a & b for merge tests")

if False: # profiling
  import cProfile

  cProfile.run ("main()", "/tmp/bfsync2-profile-merge-test")
else:
  main()
