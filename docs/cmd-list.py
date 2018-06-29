#!/usr/bin/python

import re

def parse (filename):
  f = open (filename)

  state = 0
  for line in f:
    line = line.strip()
    if line == "NAME":
      state = 1
    elif (state == 1) and (line == "----"):
      state = 2
    elif (state == 2):
      m = re.search ('(.*) - (.*)', line)
      if m:
        return m.group (1), m.group(2)
      break

def parse_cat (cat):
  cmdlist = "command-list.txt"
  cmdf = open (cmdlist)
  cmd_outf = open ("cmds-" + cat + ".txt", "w")
  for cmd_line in cmdf:
    cmd_line = cmd_line.strip()
    m = re.search ('(\S+)\s+(\S+)', cmd_line)
    if not m:
      print "ERROR: file '%s' cmd '%s' lacks category" % (cmdlist, cmd_line)
      exit (1)
    cmd, cmd_cat = (m.group (1), m.group (2))

    if cmd_cat == cat:
      result = parse (cmd + ".txt")
      if not result:
        print "ERROR parsing '" + cmd + ".txt'"
        exit(1)

      name, desc = result
      cmd_outf.write ("linkbfsync:" + name + "[1]::\n")
      cmd_outf.write ("    " + desc + ".\n\n")

for cat in "main", "add":
  parse_cat (cat)
