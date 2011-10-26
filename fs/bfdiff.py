#!/usr/bin/python

import sqlite3
import CfgParser
import sys

def parse_config (filename):
  bfsync_info = CfgParser.CfgParser (filename,
  [
  ],
  [
    "repo-type",
    "repo-path",
    "mount-point",
    "cached-inodes",
    "cached-dirs",
    "sqlite-sync",
  ])
  return bfsync_info

cfg = parse_config ("test/.bfsync/config")

sqlite_sync = cfg.get ("sqlite-sync")
if len (sqlite_sync) != 1:
  raise Exception ("bad sqlite-sync setting")

if sqlite_sync[0] == "0":
  sqlite_sync = False
else:
  sqlite_sync = True

conn = sqlite3.connect ('test/db')
c = conn.cursor()
if not sqlite_sync:
  c.execute ('''PRAGMA synchronous=off''')

version_a = int (sys.argv[1])
version_b = int (sys.argv[2])

def dict_from_table (table, n_keys, version):
  d = dict()
  c.execute ('''SELECT * FROM %s WHERE %d >= vmin and %d <= vmax''' % (table, version, version))
  for row in c:
    key = row[2:2 + n_keys]
    d[key] = row[2 + n_keys:]
  return d

def key_to_str (k):
  s = ""
  for x in k:
    if s == "":
      s += "%s" % x
    else:
      s += " %s" % x
  return s

def mklist (k):
  l = []
  for x in k:
    l += [ "%s" % x ]
  return l

def write1change (change_list):
  for s in change_list:
    sys.stdout.write (s + "\0")

def print_changes (change_type, dict_a, dict_b):
  change_list = []
  for k in dict_a:
    if k in dict_b:
      if dict_a[k] == dict_b[k]:
        # identical
        pass
      else:
        # attribute change
        change = [ change_type + "!"] + mklist (k)
        for i in range (len (dict_b[k])):
          if dict_a[k][i] == dict_b[k][i]:
            change += [ "" ]
          else:
            change += [ "%s" % dict_b[k][i] ]
        change_list += [ change ]
    else:
      # entry deleted
      change_list += [ [ change_type + "-" ] + mklist (k) ]

  for k in dict_b:
    if k in dict_a:
      # attribute change <-> handled above
      pass
    else:
      # entry added
      change_list += [ [ change_type + "+" ] + mklist (k) + mklist (dict_b[k]) ]
  # sort changes (for better compression)
  change_list.sort()
  # write changes to stdout
  for change in change_list:
    write1change (change)

dict_a = dict_from_table ("links", 2, version_a)
dict_b = dict_from_table ("links", 2, version_b)
print_changes ("l", dict_a, dict_b)

dict_a = dict_from_table ("inodes", 1, version_a)
dict_b = dict_from_table ("inodes", 1, version_b)
print_changes ("i", dict_a, dict_b)
