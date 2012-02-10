#!/usr/bin/python

import time
import sys
import os
import cPickle

CONTINUE=1
DONE=2

class CommandState:
  pass

class CommitCommand:
  def __init__ (self, arg):
    self.state = CommandState()
    self.state.arg = arg
    self.state.count = 0

  def begin (self):
    print "begin... %s" % self.state.arg
    time.sleep (1)
    return True

  def execute (self):
    self.state.count += 1
    print "execute... %s/%d" % (self.state.arg, self.state.count)
    time.sleep (1)
    return CONTINUE if self.state.count < 10 else DONE

  def finish (self):
    print "finish... %s" % self.state.arg
    time.sleep (1)

def mk_journal_entry (cmd):
  f = open ("cmdtest-journal", "w")
  f.write ("%s\n" % cPickle.dumps (cmd.state))
  f.close()

def rm_journal_entry (cmd):
  os.remove ("cmdtest-journal")

def chk_journal_entry ():
  try:
    f = open ("cmdtest-journal", "r")
    return cPickle.loads (f.read())
  except:
    pass

def run_command (cmd):
  if not cmd.begin():
    print "cmd fail in begin"
    sys.exit (1)

  mk_journal_entry (cmd)

  while True:
    ret = cmd.execute()
    if ret == DONE:
      break

  rm_journal_entry (cmd)
  cmd.finish()

je = chk_journal_entry()
if je:
  x = CommitCommand (None)
  x.state = je
  run_command (x)
else:
  x = CommitCommand (sys.argv[1])
  run_command (x)
