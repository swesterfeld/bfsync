# bfsync: Big File synchronization tool

# Copyright (C) 2012 Stefan Westerfeld
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

import bfsyncdb
import cPickle

cmd_stack = [ [] ]

CMD_DONE = 1
CMD_AGAIN = 2

def mk_journal_entry (repo):
  jentry = bfsyncdb.JournalEntry()
  jentry.operation = "nop"

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

def run_continue (repo, je):
  from commitutils import CommitCommand, RevertCommand
  from applyutils import ApplyCommand
  global cmd_stack

  assert (len (cmd_stack) == 1 and len (cmd_stack[0]) == 0)
  cmd_stack = []

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
      else:
        raise Exception ("unsupported command type %s during continue" % cmd[0])

      instance.set_state (cmd[1])
      instance.restart (repo)
      cmd_stack[-1].append (instance)

  run_commands (repo)
