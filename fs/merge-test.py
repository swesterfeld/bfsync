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
  print "%20s -> %s" % (t[1], t[2])
