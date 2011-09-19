#!/usr/bin/python

import os
import sys
import subprocess
import time
import traceback

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
  if subprocess.call (["mkdir", "-p", "test/git"]) != 0:
    print "error during setup"
    sys.exit (1)
  if subprocess.call (["git", "init", "-q", "test/git"]) != 0:
    print "error during setup"
    sys.exit (1)
  if subprocess.call (["mkdir", "-p", "test/data/subdir/subsub"]) != 0:
    print "error during setup"
    sys.exit (1)
  if subprocess.call (["cp", "-a", "../README", "test/data/README"]) != 0:
    print "error during setup"
    sys.exit (1)
  write_file ("test/data/subdir/x", "File X\n")

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

def test_read():
  if read_file ("mnt/README") != read_file ("test/data/README"):
    raise Exception ("read failed")

tests += [ ("read", test_read) ]

#####

def test_01():
  bla = "blablabla\n*\nxyz\n"
  write_file ("mnt/testx", bla);
  if read_file ("mnt/testx") != bla:
    raise Exception ("read back failed")

tests += [ ("write/read", test_01) ]

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

def test_05():
  if len (read_file ("mnt/subdir/x")) < 5:
    raise Exception ("subdir/x too small?")
  os.remove ("mnt/subdir/x")
  if os.path.exists ("mnt/subdir/x"):
    raise Exception ("subdir file not properly deleted")

tests += [ ("subdir file delete", test_05) ]

#####

def test_06():
  os.mkdir ("mnt/newdir")
  if not os.path.exists ("mnt/newdir"):
    raise Exception ("newdir not created")
  bla = "newdir test!\n"
  write_file ("mnt/newdir/testx", bla);
  if read_file ("mnt/newdir/testx") != bla:
    raise Exception ("read back failed")

tests += [ ("mkdir", test_06) ]

#####

def test_07():
  os.mkdir ("mnt/subdir/newdir")
  if not os.path.exists ("mnt/subdir/newdir"):
    raise Exception ("newdir not created")
  bla = "newdir test!\n"
  write_file ("mnt/subdir/newdir/testx", bla);
  if read_file ("mnt/subdir/newdir/testx") != bla:
    raise Exception ("read back failed")

tests += [ ("mkdir in subdir", test_07) ]

#####

def test_rmdir():
  os.mkdir ("mnt/newdir")
  if not os.path.exists ("mnt/newdir"):
    raise Exception ("newdir not created")
  os.rmdir ("mnt/newdir")
  if os.path.exists ("mnt/newdir"):
    raise Exception ("newdir not deleted")

tests += [ ("rmdir", test_rmdir) ]

#####

def test_rmdir2():
  if not os.path.exists ("mnt/subdir"):
    raise Exception ("missing subdir")
  try:
    os.rmdir ("mnt/subdir")
  except:
    pass
  if not os.path.exists ("mnt/subdir"):
    raise Exception ("subdir deleted, although not empty")

tests += [ ("rmdir non-empty", test_rmdir2) ]

#####

def test_rmdir3():
  if not os.path.exists ("mnt/subdir/subsub"):
    raise Exception ("missing subsub")
  os.rmdir ("mnt/subdir/subsub")
  if os.path.exists ("mnt/subdir/subsub"):
    raise Exception ("subsub removed but still there")
  if not os.path.exists ("mnt/subdir"):
    raise Exception ("subdir vanished")

tests += [ ("rmdir on subsubdir", test_rmdir3) ]

#####

def test_rm():
  os.remove ("mnt/subdir/x")
  if not os.path.exists ("mnt/subdir"):
    raise Exception ("subdir vanished")

tests += [ ("rm in subdir", test_rm) ]

#####

def test_commit_read():
  write_file ("mnt/foo", "foo")
  readme = read_file ("mnt/foo")
  if subprocess.call (["bfsync2", "commit", "-m", "fstest", "mnt"]) != 0:
    raise Exception ("commit failed")
  readme_committed = read_file ("mnt/foo")
  if readme != readme_committed:
    raise Exception ("README reread failed")

tests += [ ("commit-read", test_commit_read) ]

#####

# unmount mnt just in case its mounted
subprocess.call (["fusermount", "-u", "mnt"])

if subprocess.call (["bfsyncfs", "mnt"]) != 0:
  print "can't start bfsyncfs"
  sys.exit (1)

for (desc, f) in tests:
  print "test %-30s" % desc,
  teardown()
  setup()
  time.sleep (0.5)
  try:
    if subprocess.call (["tar", "cf", "test_data_before.tar", "test/data"]) != 0:
      raise Exception ("error during tar")
    f()
    if subprocess.call (["tar", "cf", "test_data_after.tar", "test/data"]) != 0:
      raise Exception ("error during tar")
    if read_file ("test_data_before.tar") != read_file ("test_data_after.tar"):
      raise Exception ("test/data changed (tar)")
  except Exception, e:
    print "FAIL: ", e
    print "\n\n"
    print "=================================================="
    traceback.print_exc()
    print "=================================================="
    print "\n\n"
  else:
    print "OK."
  finally:
    try:
      os.remove ("test_data_before.tar")
      os.remove ("test_data_after.tar")
    except:
      pass
teardown()
setup()

if subprocess.call (["fusermount", "-u", "mnt"]):
  print "can't stop bfsyncfs"
  sys.exit (1)
