#!/usr/bin/python

import os
import sys
import subprocess

def teardown():
  cwd = os.getcwd()
  if subprocess.call (["rm", "-rf", cwd + "/test"]) != 0:
    print "error during teardown"
    sys.exit (1)

def setup():
  cwd = os.getcwd()
  if subprocess.call (["mkdir", "-p", "test/new"]) != 0:
    print "error during setup"
    sys.exit (1)
  if subprocess.call (["mkdir", "-p", "test/data/subdir"]) != 0:
    print "error during setup"
    sys.exit (1)
  if subprocess.call (["cp", "-a", "../README", "test/data/README"]) != 0:
    print "error during setup"
    sys.exit (1)
  write_file ("test/data/subdir/x", "File X")

def write_file (name, data):
  f = open (name, "w")
  f.write (data)
  f.close()

def read_file (name):
  f = open (name)
  data = f.read()
  f.close()
  return data

# write/read test
def test_01():
  bla = "blablabla\n*\nxyz\n"
  write_file ("testx", bla);
  if read_file ("testx") != bla:
    raise Exception ("read back failed")

# Overwrite README
def test_02():
  bla = "blablabla\n*\nxyz\n"
  write_file ("mnt/README", bla)
  if read_file ("mnt/README") != bla:
    raise Exception ("read back failed")
  os.remove ("mnt/README")
  if os.path.exists ("mnt/README"):
    raise Exception ("File not properly deleted")

# Write file in subdir
def test_03():
  bla = "blablabla\n*\nxyz\n"
  write_file ("mnt/subdir/y", bla)
  if read_file ("mnt/subdir/y") != bla:
    raise Exception ("read back failed")

tests = [ (test_01, "test_01"),
          (test_02, "test_02"),
          (test_03, "test_03") ]

for (f, desc) in tests:
  print desc, "...",
  teardown()
  setup()
  try:
    f()
  except Exception, e:
    print "FAIL: ", e
  else:
    print "OK."

teardown()
