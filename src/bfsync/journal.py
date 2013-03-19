# bfsync: Big File synchronization tool
# Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

import bfsyncdb
import cPickle
import os
from ServerConn import ServerConn

class JournalOperation:
  pass

cmd_stack = [ [] ]
cmd_op = JournalOperation()

CMD_DONE = 1
CMD_AGAIN = 2

def init_journal (command_line):
  cmd_op.pid = os.getpid()
  cmd_op.command_line = command_line

def mk_journal_entry (repo):
  jentry = bfsyncdb.JournalEntry()
  jentry.operation = cPickle.dumps (cmd_op)

  all_state_list = []
  for cmds in cmd_stack:
    state_list = []
    for cmd in cmds:
      state_list += [ (cmd.get_operation(), cmd.get_state()) ]
    all_state_list.append (state_list)

  jentry.state = cPickle.dumps (all_state_list)
  repo.bdb.clear_journal_entries()
  repo.bdb.store_journal_entry (jentry)

def queue_command (cmd):
  cmd_stack[-1].append (cmd)

def print_stack():
  print "\r==================S=T=A=C=K===================="
  level = 0
  for cmds in cmd_stack:
    print "["
    for cmd in cmds:
      print " %d * " % level, cmd.get_operation()
    print "]"
    level += 1
  print "==============================================="

def run_commands (repo):
  global cmd_stack
  while len (cmd_stack) > 1 or len (cmd_stack[0]):
    repo.bdb.begin_transaction()
    mk_journal_entry (repo)
    repo.bdb.commit_transaction()
    if len (cmd_stack[-1]) == 0:
      cmd_stack.pop()
    else:
      exec_item = len (cmd_stack) - 1
      cmd = cmd_stack[exec_item][0]
      cmd_stack.append ([])
      rc = cmd.execute()
      if rc != CMD_DONE and rc != CMD_AGAIN:
        raise Exception ("bad return code (%s) from command %s execute()" % (rc, cmd.get_operation()))
      if rc == CMD_DONE:
        cmd_stack[exec_item].pop (0)
  repo.bdb.begin_transaction()
  repo.bdb.clear_journal_entries()
  repo.bdb.commit_transaction()

  # notify server: operation is no longer active => re-enter read/write mode
  try:
    server_conn = ServerConn (repo.path)
    server_conn.get_lock()
    server_conn.update_read_only()
    server_conn.close()
  except IOError, e:
    pass       # no server => no notification

def run_continue (repo, je):
  from commitutils import CommitCommand, RevertCommand
  from applyutils import ApplyCommand
  from transferutils import FastForwardCommand, MergeCommand
  global cmd_stack
  global cmd_op

  assert (len (cmd_stack) == 1 and len (cmd_stack[0]) == 0)
  cmd_stack = []

  cmd_op = cPickle.loads (je.operation)
  cmd_op.pid = os.getpid()                # replace pid with the pid of the continue process

  state = cPickle.loads (je.state)
  for cmds in state:
    cmd_stack.append ([])
    for cmd in cmds:
      if cmd[0] == "commit":
        instance = CommitCommand()
      elif cmd[0] == "revert":
        instance = RevertCommand()
      elif cmd[0] == "apply":
        instance = ApplyCommand()
      elif cmd[0] == "fast-forward":
        instance = FastForwardCommand()
      elif cmd[0] == "merge":
        instance = MergeCommand()
      else:
        raise Exception ("unsupported command type %s during continue" % cmd[0])

      instance.set_state (cmd[1])
      instance.restart (repo)
      cmd_stack[-1].append (instance)

  run_commands (repo)
