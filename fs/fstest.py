#!/usr/bin/python

import os
import sys
import subprocess
import time
import traceback
import argparse
from stat import *

class FuseFS:
  def init (self):
    cwd = os.getcwd()
    if subprocess.call (["mkdir", "-m", "0700", "-p", "test/new",
                                                      "test/objects",
                                                      "test/.bfsync"]) != 0:
      raise Exception ("error during setup (can't create dirs)")
    if subprocess.call (["mkdir", "-p", "mnt"]) != 0:
      raise Exception ("error during setup (can't create dirs)")
    for i in range (0, 256):
      os.mkdir ("test/new/%02x" % i, 0700)
    if subprocess.call (["./setupdb.py"]) != 0:
      raise Exception ("error during setup")
    start_bfsyncfs()

  def commit (self):
    cwd = os.getcwd()
    os.chdir ("mnt")
    if run_quiet ([cwd + "/bfsync2", "commit", "-m", "fstest"]) != 0:
      raise Exception ("commit failed")
    os.chdir (cwd)

  def teardown (self):
    cwd = os.getcwd()
    if os.path.exists ("mnt/.bfsync"):
      if subprocess.call (["fusermount", "-u", "mnt"]):
        print "can't stop bfsyncfs"
        sys.exit (1)
    if subprocess.call (["rm", "-rf", cwd + "/test/new", cwd + "/test/objects", cwd + "/test/.bfsync"]) != 0:
      print "error during teardown"
      sys.exit (1)

  def umount (self):
    if subprocess.call (["fusermount", "-u", "mnt"]):
      print "can't stop bfsyncfs"
      sys.exit (1)

class NativeFS:
  def init (self):
    pass

  def commit (self):
    pass

  def teardown (self):
    cwd = os.getcwd()
    if subprocess.call (["rm", "-rf", cwd + "/mnt"]) != 0:
      print "error during teardown"
      sys.exit (1)
    if subprocess.call (["mkdir", "-p", cwd + "/mnt"]) != 0:
      print "error during teardown"
      sys.exit (1)

  def umount (self):
    self.teardown()

def teardown():
  fs.teardown()

def setup():
  fs.init()
  if subprocess.call (["mkdir", "-p", "mnt/subdir/subsub"]) != 0:
    raise Exception ("error during setup")
  if subprocess.call (["cp", "-a", "../README", "mnt/README"]) != 0:
    raise Exception ("error during setup")
  write_file ("mnt/subdir/x", "File X\n")
  commit()

def clear_cache():
  cwd = os.getcwd()
  os.chdir ("mnt")
  if run_quiet ([cwd + "/bfsync2", "debug-clear-cache"]) != 0:
    raise Exception ("commit failed")
  os.chdir (cwd)

def write_file (name, data):
  f = open (name, "w")
  f.write (data)
  f.close()

def read_file (name):
  f = open (name)
  data = f.read()
  f.close()
  return data

tests      = []
bf_tests   = []   # tests that only work on FuseFS
root_tests = []   # tests that only work as root

def test_read():
  if read_file ("mnt/README") != read_file ("../README"):
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
  for name in os.listdir ("mnt"):
    if name == "newdir":
      raise Exception ("newdir shows up in listdir after delete")

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
  commit()
  readme_committed = read_file ("mnt/foo")
  if readme != readme_committed:
    raise Exception ("README reread failed")

tests += [ ("commit-read", test_commit_read) ]

#####

def test_commit_mtime():
  write_file ("mnt/foo", "foo")
  os.system ("touch -t 01010101 mnt/foo")
  old_stat = os.stat ("mnt/foo")
  commit()
  new_stat = os.stat ("mnt/foo")
  if old_stat.st_mtime != new_stat.st_mtime:
    raise Exception ("stat mtime diffs %d => %d" % (old_stat.st_mtime, new_stat.st_mtime))

tests += [ ("commit-mtime", test_commit_mtime) ]

#####

def test_commit_mtime_chmod():
  write_file ("mnt/foo", "foo")
  os.system ("touch -t 01010101 mnt/foo")
  old_stat = os.stat ("mnt/foo")
  commit()
  os.chmod ("mnt/foo", 0640)
  new_stat = os.stat ("mnt/foo")
  if old_stat.st_mtime != new_stat.st_mtime:
    raise Exception ("stat mtime diffs %d => %d" % (old_stat.st_mtime, new_stat.st_mtime))

tests += [ ("commit-mtime-chmod", test_commit_mtime_chmod) ]

#####

def test_commit_uid_gid():
  write_file ("mnt/foo", "foo")
  os.chown ("mnt/foo", 123, 456)
  old_stat = os.stat ("mnt/foo")
  if (old_stat.st_uid != 123 or old_stat.st_gid != 456):
    raise Exception ("can't set uid/gid (are you root?)")
  commit()
  new_stat = os.stat ("mnt/foo")
  if old_stat.st_uid != new_stat.st_uid:
    raise Exception ("stat uid diffs %d => %d" % (old_stat.st_uid, new_stat.st_uid))
  if old_stat.st_gid != new_stat.st_gid:
    raise Exception ("stat gid diffs %d => %d" % (old_stat.st_gid, new_stat.st_gid))

root_tests += [ ("commit-uid-gid", test_commit_uid_gid) ]

#####

def test_commit_symlink():
  link = "README"
  os.symlink (link, "mnt/readme-link")
  if os.readlink ("mnt/readme-link") != link:
    raise Exception ("cannot create symlink")
  commit()
  new_link = os.readlink ("mnt/readme-link")
  if new_link != link:
    raise Exception ("symlink diffs %s => %s" % (link, new_link))

tests += [ ("commit-symlink", test_commit_symlink) ]

#####

def test_commit_dot_git():
  dot_git = "dot git"
  write_file ("mnt/.git", dot_git)
  if read_file ("mnt/.git") != dot_git:
    raise Exception ("cannot create dot git file")
  commit()
  if read_file ("mnt/.git") != dot_git:
    raise Exception ("cannot reread dot git file")

tests += [ ("commit-dot-git", test_commit_dot_git) ]

#####

def test_commit_subdir():
  test = "blub\n"
  write_file ("mnt/subdir/blub", test)
  if read_file ("mnt/subdir/blub") != test:
    raise Exception ("cannot create subdir file")
  commit()
  if read_file ("mnt/subdir/blub") != test:
    raise Exception ("cannot reread subdir file")

tests += [ ("commit-subdir", test_commit_subdir) ]

#####

def test_commit_dir_mtime():
  os.mkdir ("mnt/timetest")
  if not os.path.exists ("mnt/timetest"):
    raise Exception ("timetest not created")
  os.system ("touch -t 01010101 mnt/timetest")
  old_stat = os.stat ("mnt/timetest")
  commit()
  new_stat = os.stat ("mnt/timetest")
  if old_stat.st_mtime != new_stat.st_mtime:
    raise Exception ("stat mtime diffs %d => %d" % (old_stat.st_mtime, new_stat.st_mtime))

tests += [ ("commit-dir-mtime", test_commit_dir_mtime) ]

#####

def test_commit_dir_mtime2():
  os.mkdir ("mnt/timetest")
  write_file ("mnt/timetest/foo", "foo\n")
  if not os.path.exists ("mnt/timetest"):
    raise Exception ("timetest not created")
  os.system ("touch -t 01010101 mnt/timetest")
  old_stat = os.stat ("mnt/timetest")
  commit()
  new_stat = os.stat ("mnt/timetest")
  if old_stat.st_mtime != new_stat.st_mtime:
    raise Exception ("stat mtime diffs %d => %d" % (old_stat.st_mtime, new_stat.st_mtime))

tests += [ ("commit-dir-mtime2", test_commit_dir_mtime2) ]

#####

def test_commit_dir_mtime3():
  x = read_file ("mnt/subdir/x")
  os.system ("touch -t 01010101 mnt/subdir")
  old_stat = os.stat ("mnt/subdir")
  commit()
  new_stat = os.stat ("mnt/subdir")
  y = read_file ("mnt/subdir/x")
  if x != y:
    raise Exception ("subdir/x changed")
  if old_stat.st_mtime != new_stat.st_mtime:
    raise Exception ("stat mtime diffs %d => %d" % (old_stat.st_mtime, new_stat.st_mtime))

tests += [ ("commit-dir-mtime3", test_commit_dir_mtime3) ]

#####

def test_commit_chmod():
  os.chmod ("mnt/subdir/x", 0400)
  old_stat = os.stat ("mnt/subdir/x")
  commit()
  new_stat = os.stat ("mnt/subdir/x")
  if old_stat.st_mode != new_stat.st_mode:
    raise Exception ("stat mode diffs %o => %o" % (old_stat.st_mode, new_stat.st_mode))

tests += [ ("commit-chmod", test_commit_chmod) ]

#####

def test_commit_chmod2():
  os.chmod ("mnt/subdir", 0700)
  old_stat = os.stat ("mnt/subdir")
  commit()
  write_file ("mnt/subdir/x", "changed x\n")
  commit()
  new_stat = os.stat ("mnt/subdir")
  if old_stat.st_mode != new_stat.st_mode:
    raise Exception ("stat mode diffs %o => %o" % (old_stat.st_mode, new_stat.st_mode))

tests += [ ("commit-chmod2", test_commit_chmod2) ]

#####

def test_commit_symlink_cow():
  os.symlink ("README", "mnt/readme-link")
  if os.system ("touch -h -t 01010101 mnt/readme-link") != 0:
    raise Exception ("can't set initial (commit) time")
  commit()
  if os.system ("touch -h -t 02020202 mnt/readme-link") != 0:
    raise Exception ("can't modify time")
  new_stat = os.lstat ("mnt/readme-link")

tests += [ ("commit-symlink-cow", test_commit_symlink_cow) ]

#####

def test_commit_rm():
  if len (read_file ("mnt/README")) < 100:
    raise Exception ("README too small?")
  os.remove ("mnt/README")
  if os.path.exists ("mnt/README"):
    raise Exception ("File not properly deleted")
  commit()
  if os.path.exists ("mnt/README"):
    raise Exception ("File not properly deleted after commit")

tests += [ ("commit-rm-README", test_commit_rm) ]

#####

def test_commit_rmdir():
  os.mkdir ("mnt/newdir")
  if not os.path.exists ("mnt/newdir"):
    raise Exception ("newdir not created")
  commit()
  if not os.path.exists ("mnt/newdir"):
    raise Exception ("newdir not there after commit")
  os.rmdir ("mnt/newdir")
  if os.path.exists ("mnt/newdir"):
    raise Exception ("newdir not deleted")
  commit()
  if os.path.exists ("mnt/newdir"):
    raise Exception ("newdir not deleted after commit")

tests += [ ("commit-rmdir", test_commit_rmdir) ]

#####

def test_stat_ms():
  fn = "/tmp/fstest_naietrdn"
  write_file (fn, "foo")
  mtime_old = os.stat (fn).st_mtime
  os.system ("cp -a %s mnt/foo" % fn)
  mtime_new = os.stat ("mnt/foo").st_mtime
  if mtime_old != mtime_new:
    raise Exception ("cp mtime changes => %f -> %f" % (mtime_old, mtime_new))
  commit()
  mtime_commit = os.stat ("mnt/foo").st_mtime
  if mtime_commit != mtime_old:
    raise Exception ("commit mtime changes => %f -> %f" % (mtime_old, mtime_commit))
  os.remove (fn)

tests += [ ("test-stat-ms", test_stat_ms) ]

#####

def test_dir_mode():
  os.chmod ("mnt/subdir", 0700)
  mode_old = os.stat ("mnt/subdir").st_mode
  commit()
  os.system ("touch mnt/subdir")
  mode_new = os.stat ("mnt/subdir").st_mode
  if mode_old != mode_new:
    raise Exception ("mode diffs %o => %o" % (mode_old, mode_new))

tests += [ ("test-dir-mode", test_dir_mode) ]

#####

def test_commit_uid_gid_cow():
  os.chmod ("mnt/subdir", 0777)
  os.chown ("mnt/subdir", 123, 456)
  old_stat = os.stat ("mnt/subdir")
  if (old_stat.st_uid != 123 or old_stat.st_gid != 456):
    raise Exception ("can't set uid/gid (are you root?)")
  commit()
  write_file ("mnt/subdir/y", "Y!\n")
  new_stat = os.stat ("mnt/subdir")
  if old_stat.st_uid != new_stat.st_uid:
    raise Exception ("stat uid diffs %d => %d" % (old_stat.st_uid, new_stat.st_uid))
  if old_stat.st_gid != new_stat.st_gid:
    raise Exception ("stat gid diffs %d => %d" % (old_stat.st_gid, new_stat.st_gid))

root_tests += [ ("commit-uid-gid-cow", test_commit_uid_gid_cow) ]

#####

def test_commit_special():
  for mode, name in [(S_IFIFO, "fifo"),
                     (S_IFSOCK, "socket")]:
    os.mknod ("mnt/" + name, mode | 0644)
    old_stat = os.stat ("mnt/" + name)
    commit()
    new_stat = os.stat ("mnt/" + name)
    if (old_stat.st_mode != new_stat.st_mode):
      raise Exception ("stat diffs with %s %o => %o", name, old_stat.st_mode, new_stat.st_mode)
    if old_stat.st_ctime == 0:
      raise Exception ("old ctime zero on %s" % name)
    if old_stat.st_mtime == 0:
      raise Exception ("old mtime zero on %s" % name)
    if new_stat.st_ctime == 0:
      raise Exception ("new ctime zero on %s" % name)
    if new_stat.st_mtime == 0:
      raise Exception ("new mtime zero on %s" % name)

tests += [ ("commit-special", test_commit_special) ]

#####

def test_commit_device():
  for mode, name in [(S_IFBLK, "block"), (S_IFCHR, "char")]:
    os.mknod ("mnt/" + name, mode | 0644, os.makedev (42, 23))
    old_stat = os.stat ("mnt/" + name)
    if os.major (old_stat.st_rdev) != 42:
      raise Exception ("device is not major 42")
    if os.minor (old_stat.st_rdev) != 23:
      raise Exception ("device is not minor 23")
    commit()
    new_stat = os.stat ("mnt/" + name)
    if (old_stat.st_mode != new_stat.st_mode):
      raise Exception ("stat diffs with %s %o => %o" % (name, old_stat.st_mode, new_stat.st_mode))
    if (old_stat.st_rdev != new_stat.st_rdev):
      raise Exception ("%s device diff (%d,%d) => (%d,%d)" % (name,
        os.major (old_stat.st_rdev), os.minor (old_stat.st_rdev),
        os.major (new_stat.st_rdev), os.minor (new_stat.st_rdev)))

root_tests += [ ("commit-device", test_commit_device) ]

#####

def test_rename():
  orig_readme = read_file ("mnt/README")
  os.rename ("mnt/README", "mnt/xreadme")
  if read_file ("mnt/xreadme") != orig_readme:
    raise Exception ("contents changed during rename")
  try:
    os.stat ("mnt/README")
  except:
    pass
  else:
    raise Exception ("rename left original file behind (stat ok)")
  try:
    read_file ("mnt/README")
  except:
    pass
  else:
    raise Exception ("rename left original file behind (read ok)")

tests += [ ("rename", test_rename) ]

#####

def test_chmod_ctime():
  old_stat = os.stat ("mnt/README")
  time.sleep (0.1)
  os.chmod ("mnt/README", 0600)
  new_stat = os.stat ("mnt/README")
  if old_stat.st_ctime == new_stat.st_ctime:
    raise Exception ("ctime unchanged after chmod")

tests += [ ("chmod-ctime", test_chmod_ctime) ]

#####

def test_chown_ctime():
  old_stat = os.stat ("mnt/README")
  time.sleep (0.1)
  os.chown ("mnt/README", 123, 456)
  new_stat = os.stat ("mnt/README")
  if old_stat.st_ctime == new_stat.st_ctime:
    raise Exception ("ctime unchanged after chown")

root_tests += [ ("chown-ctime", test_chown_ctime) ]

#####

def test_truncate_ctime():
  old_stat = os.stat ("mnt/README")
  time.sleep (0.1)
  if os.system ("bftesthelper truncate mnt/README 0") != 0:
    raise Exception ("error truncating via bftesthelper")
  new_stat = os.stat ("mnt/README")
  if old_stat.st_ctime == new_stat.st_ctime:
    raise Exception ("ctime unchanged after truncate")

tests += [ ("truncate-ctime", test_truncate_ctime) ]

#####

def test_commit_ctime():
  os.chmod ("mnt/README", 0600)
  old_stat = os.stat ("mnt/README")
  commit()
  new_stat = os.stat ("mnt/README")
  if old_stat.st_ctime != new_stat.st_ctime:
    raise Exception ("ctime not stored by commit")

tests += [ ("commit-ctime", test_commit_ctime) ]

#####

def test_mtime_ctime_never_zero():
  write_file ("mnt/newfile", "unimportant content")
  os.mkdir ("mnt/newdir")
  for i in [ "mnt/README", "mnt/subdir", "mnt/newfile", "mnt/newdir" ]:
    stat = os.stat (i)
    if stat.st_ctime == 0:
      raise Exception ("ctime zero on %s" % i)
    if stat.st_mtime == 0:
      raise Exception ("mtime zero on %s" % i)

tests += [ ("mtime-ctime-never-zero", test_mtime_ctime_never_zero) ]

#####

def test_commit_same():
  write_file ("mnt/subdir/file1", "unimportant content")
  write_file ("mnt/subdir/file2", "unimportant content")
  commit()

tests += [ ("commit-same", test_commit_same) ]

#####

def test_dir_rename():
  x = read_file ("mnt/subdir/x")
  os.rename ("mnt/subdir", "mnt/rendir")
  x1 = read_file ("mnt/rendir/x")
  if x != x1:
    raise Exception ("x not ok in renamed directory before commit")
  commit()
  x2 = read_file ("mnt/rendir/x")
  if x != x2:
    raise Exception ("x not ok in renamed directory after commit")

tests += [ ("dir-rename", test_dir_rename) ]

#####

def test_mode_after_append():
  os.chmod ("mnt/README", 0600)
  old_stat = os.stat ("mnt/README")
  f = open ("mnt/README", "a")
  f.write ("foo")
  f.close()
  new_stat = os.stat ("mnt/README")
  if old_stat.st_mode != new_stat.st_mode:
    raise Exception ("mode changed after append")

tests += [ ("mode-after-append", test_mode_after_append) ]

#####

def test_ctime_after_append():
  old_stat = os.stat ("mnt/README")
  time.sleep (0.1)
  f = open ("mnt/README", "a")
  f.write ("foo")
  f.close()
  new_stat = os.stat ("mnt/README")
  if old_stat.st_ctime == new_stat.st_ctime:
    raise Exception ("ctime not changed after append")

tests += [ ("ctime-after-append", test_ctime_after_append) ]

#####

def test_ctime_after_append_commit():
  old_stat = os.stat ("mnt/README")
  time.sleep (0.1)
  f = open ("mnt/README", "a")
  f.write ("foo")
  f.close()
  commit()
  new_stat = os.stat ("mnt/README")
  if old_stat.st_ctime == new_stat.st_ctime:
    raise Exception ("ctime not changed after append+commit")

tests += [ ("ctime-after-append-commit", test_ctime_after_append_commit) ]

#####

def test_symlink_uid():
  link = "README"
  os.symlink (link, "mnt/readme-link")
  stat_file = os.lstat ("mnt/README")
  stat_link = os.lstat ("mnt/readme-link")
  if stat_file.st_uid != stat_link.st_uid:
    raise Exception ("uid diffs %d => %d" % (stat_file.st_uid, stat_link.st_uid))
  if stat_file.st_gid != stat_link.st_gid:
    raise Exception ("gid diffs %d => %d" % (stat_file.st_gid, stat_link.st_gid))

tests += [ ("symlink-uid", test_symlink_uid) ]

#####

def test_symlink_mode():
  link = "README"
  os.symlink (link, "mnt/readme-link")
  stat_link = os.lstat ("mnt/readme-link")
  if stat_link.st_mode & 0777 != 0777:
    raise Exception ("mode diffs mode %o != %o" % (stat_link.st_mode & 0777, 0777))

tests += [ ("symlink-mode", test_symlink_mode) ]

#####

def test_rmdir_create():
  os.mkdir ("mnt/foo")
  os.rmdir ("mnt/foo")
  write_file ("mnt/foo", "test-content")

tests += [ ("rmdir-create", test_rmdir_create) ]

#####

def test_open_dir_ctime():
  stat_subdir = os.lstat ("mnt/subdir")
  time.sleep (0.1)
  write_file ("mnt/subdir/newfile", "test-content")
  new_stat_subdir = os.lstat ("mnt/subdir")
  if stat_subdir.st_mtime == new_stat_subdir.st_mtime:
    raise Exception ("open did not change mtime")
  if stat_subdir.st_ctime == new_stat_subdir.st_ctime:
    raise Exception ("open did not change mtime")

tests += [ ("open-dir-ctime", test_open_dir_ctime) ]

#####

def test_top_dir_mode():
  os.chmod ("mnt", 0700)
  stat_mnt = os.lstat ("mnt")
  if (stat_mnt.st_mode & 0777) != 0700:
    raise Exception ("chmod incorrect")
  commit()
  stat_mnt = os.lstat ("mnt")
  if (stat_mnt.st_mode & 0777) != 0700:
    raise Exception ("chmod not correct after commit")

tests += [ ("top-dir-mode", test_top_dir_mode) ]

#####

def test_partial_chown():
  st_old = os.lstat ("mnt/README")
  os.chown ("mnt/README", 123, -1)
  st = os.lstat ("mnt/README")
  if st_old.st_gid != st.st_gid:
    raise Exception ("gid changed for partial chown")

  st_old = os.lstat ("mnt/README")
  os.chown ("mnt/README", -1, 456)
  st = os.lstat ("mnt/README")
  if st_old.st_uid != st.st_uid:
    raise Exception ("uid changed for partial chown")

  if st.st_uid != 123 or st.st_gid != 456:
    raise Exception ("uid/gid not correct")

root_tests += [ ("partial-chown", test_partial_chown) ]

#####

def test_rename_replace():
  xsize = os.path.getsize ("mnt/README")
  write_file ("mnt/foo", "foo")
  os.rename ("mnt/README", "mnt/foo")
  if os.path.exists ("mnt/README"):
    raise Exception ("file not properly renamed")
  if os.path.getsize ("mnt/foo") != xsize:
    raise Exception ("file size is wrong")

tests += [ ("rename-replace", test_rename_replace) ]

#####

def test_link_inode():
  os.link ("mnt/README", "mnt/LREADME")
  xsize = os.path.getsize ("mnt/README")
  ysize = os.path.getsize ("mnt/LREADME")
  if xsize != ysize:
    raise Exception ("file size not identical")
  xst = os.lstat ("mnt/README")
  yst = os.lstat ("mnt/LREADME")
  if xst.st_ino != yst.st_ino:
    raise Exception ("link inode not identical")
  write_file ("mnt/README", "blub")
  xblub = read_file ("mnt/README")
  yblub = read_file ("mnt/LREADME")
  if xblub != yblub:
    raise Exception ("link change didn't affect both files")

tests += [ ("link-inode", test_link_inode) ]

#####

def test_commits_dir():
  os.stat ("mnt/.bfsync/commits")
  os.stat ("mnt/.bfsync/commits/1/README")
  os.stat ("mnt/.bfsync/commits/1/subdir")

bf_tests += [ ("commits-dir", test_commits_dir) ]

#####

def test_commits_rm():
  start_size = os.path.getsize ("mnt/README")
  os.remove ("mnt/README")
  commit()
  clear_cache()
  st = os.stat ("mnt/.bfsync/commits/1/README")
  if st.st_size != start_size:
    raise ("README in commits dir wrong")
  if os.path.exists ("mnt/.bfsync/commits/2/README"):
    raise ("README not removed in commits dir")

bf_tests += [ ("commits-rm", test_commits_rm) ]

#####


def start_bfsyncfs():
  if subprocess.call (["./bfsyncfs", "test", "mnt"]) != 0:
    print "can't start bfsyncfs"
    sys.exit (1)

def commit():
  fs.commit()

def run_quiet (cmd):
  return subprocess.Popen (cmd, stdout=subprocess.PIPE).wait()

if os.getuid() == 0:
  tests += root_tests
else:
  print "%d root tests skipped, because fstest.py is not running with uid=0.\n" % len (root_tests)

def main (fstest_args):
  # compile
  if subprocess.call (["make"]):
    print "compilation failed"
    sys.exit (1)

  # unmount if mounted
  try:
    f = open ("mnt/.bfsync/info")
    f.close()
    subprocess.call (["fusermount", "-u", "mnt"])
  except:
    pass # not mounted

  if fstest_args.r:
    teardown()
    setup()
    # umount fs
    if subprocess.call (["fusermount", "-u", "mnt"]):
      print "can't stop bfsyncfs"
      sys.exit (1)
    sys.exit (0)

  fail_count = 0
  ok_count = 0

  test_nr = 1
  for (desc, f) in tests:
    sys.stdout.write ("[%02d] test %-30s" % (test_nr, desc))
    sys.stdout.flush()
    teardown()
    setup()
    try:
      f()
    except Exception, e:
      print "FAIL: ", e
      fail_count += 1
      if fstest_args.v:
        print "\n\n"
        print "=================================================="
        traceback.print_exc()
        print "=================================================="
        print "\n\n"
    else:
      print "OK."
      ok_count += 1
    test_nr += 1
  teardown()
  setup()

  # output results
  print
  if fail_count == 0:
    print "PASSED: %d tests" % ok_count
  else:
    print "SUMMARY: %d/%d tests failed" % (fail_count, fail_count + ok_count)

  # umount fs
  fs.umount()

parser = argparse.ArgumentParser (prog='fstest.py')
parser.add_argument ('-v', action='store_true', help='verbose')
parser.add_argument ('-r', action='store_true', help='reset')
parser.add_argument ('-f', action='store_true', help='test underlying filesystem instead of fuse filesystem')
fstest_args = parser.parse_args()

if fstest_args.f:
  fs = NativeFS()
else:
  fs = FuseFS()
  tests += bf_tests

os.putenv ("BFSYNC_NO_HASH_CACHE", "1")
main (fstest_args)
