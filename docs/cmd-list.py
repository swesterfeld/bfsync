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

cmdlist = "command-list.txt"
cmdf = open (cmdlist)
for cmd in cmdf:
  cmd = cmd.strip()
  result = parse (cmd + ".1.txt")
  if not result:
    print "..."
    exit(1)

  a, b = result
  print "linkbfsync:" + a + "[1]::"
  print "    " + b + "."
  print

