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
  if subprocess.call (["mkdir", "-p", "test/del"]) != 0:
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

tests = []

#####

def test_01():
  bla = "blablabla\n*\nxyz\n"
  write_file ("testx", bla);
  if read_file ("testx") != bla:
    raise Exception ("read back failed")

tests += [ ("write/read test", test_01) ]

#####

def test_02():
  bla = "blablabla\n*\nxyz\n"
  write_file ("mnt/README", bla)
  if read_file ("mnt/README") != bla:
    raise Exception ("read back failed")
  os.remove ("mnt/README")
  if os.path.exists ("mnt/README"):
    raise Exception ("File not properly deleted")

tests += [ ("overwrite README", test_02) ]

#####

def test_03():
  bla = "blablabla\n*\nxyz\n"
  write_file ("mnt/subdir/y", bla)
  if read_file ("mnt/subdir/y") != bla:
    raise Exception ("read back failed")

tests += [ ("write file in subdir", test_03) ]

#####

def test_04():
  if len (read_file ("mnt/README")) < 100:
    raise Exception ("README too small?")
  os.remove ("mnt/README")
  if os.path.exists ("mnt/README"):
    raise Exception ("File not properly deleted")

tests += [ ("delete README", test_04) ]

#####

for (desc, f) in tests:
  print "test %-30s" % desc,
  teardown()
  setup()
  try:
    if subprocess.call (["tar", "cf", "test_data_before.tar", "test/data"]) != 0:
      raise Exception ("error during tar")
    f()
    if subprocess.call (["tar", "cf", "test_data_after.tar", "test/data"]) != 0:
      raise Exception ("error during tar")
    if read_file ("test_data_before.tar") != read_file ("test_data_after.tar"):
      raise Exception ("test/data changed (tar)")
    os.remove ("test_data_before.tar")
    os.remove ("test_data_after.tar")
  except Exception, e:
    print "FAIL: ", e
  else:
    print "OK."

teardown()
setup()
