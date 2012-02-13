#!/usr/bin/python

import time
import sys
import os
import cPickle

CONTINUE=1
DONE=2

class CommandState:
  pass

def mk_journal_entry (cmd):
  f = open ("cmdtest-journal", "w")
  f.write ("%s\n" % cPickle.dumps (cmd.get_state()))
  f.close()

def rm_journal_entry (cmd):
  os.remove ("cmdtest-journal")

def chk_journal_entry ():
  try:
    f = open ("cmdtest-journal", "r")
    return cPickle.loads (f.read())
  except:
    pass

class CommitCommand:
  def __init__ (self, arg):
    self.state = CommandState()
    self.state.arg = arg
    self.state.count = 0

  def get_state (self):
    return self.state

  def set_state (self, state):
    self.state = state

  def start (self):
    print "start... %s" % self.state.arg
    time.sleep (1)
    self.count2 = self.state.count * 2
    return True

  def restart (self):
    self.count2 = self.state.count * 2

  def execute (self):
    while self.state.count < 10:
      self.state.count += 1
      self.count2 += 2
      print "execute... %s/%d (%d)" % (self.state.arg, self.state.count, self.count2)
      time.sleep (1)
      mk_journal_entry (self)

def run_command (cmd, state = None):
  if state:
    cmd.set_state (state)
    cmd.restart()
  else:
    if not cmd.start():
      print "cmd fail in begin"
      sys.exit (1)

  mk_journal_entry (cmd)
  cmd.execute()
  rm_journal_entry (cmd)

je = chk_journal_entry()
if je:
  x = CommitCommand (None)
  run_command (x, je)
else:
  x = CommitCommand (sys.argv[1])
  run_command (x)
