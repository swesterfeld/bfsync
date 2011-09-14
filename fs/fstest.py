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
  if subprocess.call (["mkdir", "-p", "test/data"]) != 0:
    print "error during setup"
    sys.exit (1)
  if subprocess.call (["cp", "-aiv", "../README", "test/data/README"]) != 0:
    print "error during setup"
    sys.exit (1)

def test_01():
  bla = "blablabla\n*\nxyz\n"
  write_file ("testx", bla);
  if read_file ("testx") != bla:
    print "test_01 failed"
    sys.exit (1)

teardown()
setup()
test_01()

