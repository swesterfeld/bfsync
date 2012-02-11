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
    self.count2 = self.state.count * 2
    return True

  def restart (self, state):
    self.state = state
    self.count2 = self.state.count * 2

  def execute (self):
    self.state.count += 1
    self.count2 += 2
    print "execute... %s/%d (%d)" % (self.state.arg, self.state.count, self.count2)
    time.sleep (1)
    return CONTINUE if self.state.count < 10 else DONE

  def finish (self):
    print "finish... %s (%d)" % (self.state.arg, self.count2)
    time.sleep (1)

def mk_journal_entry (cmd, run_state):
  f = open ("cmdtest-journal", "w")
  f.write ("%s\n" % cPickle.dumps ((run_state, cmd.state)))
  f.close()

def rm_journal_entry (cmd):
  os.remove ("cmdtest-journal")

def chk_journal_entry ():
  try:
    f = open ("cmdtest-journal", "r")
    return cPickle.loads (f.read())
  except:
    pass

RUN_EXECUTE = 1
RUN_FINISH  = 2

def run_command (cmd, start_state = None, state = None):
  if state:
    cmd.restart (state)
  else:
    if not cmd.begin():
      print "cmd fail in begin"
      sys.exit (1)
    start_state = RUN_EXECUTE

  if start_state == RUN_EXECUTE:
    mk_journal_entry (cmd, RUN_EXECUTE)

    while True:
      ret = cmd.execute()
      if ret == DONE:
        mk_journal_entry (cmd, RUN_FINISH)
        break
      else:
        mk_journal_entry (cmd, RUN_EXECUTE)

  cmd.finish()
  rm_journal_entry (cmd)

je = chk_journal_entry()
if je:
  x = CommitCommand (None)
  run_command (x, je[0], je[1])
else:
  x = CommitCommand (sys.argv[1])
  run_command (x)
