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

def print_changes (change_type, dict_a, dict_b):
  for k in dict_a:
    if k in dict_b:
      if dict_a[k] == dict_b[k]:
        pass    # identical
      else:
        # attribute change
        print change_type, "!", key_to_str (k),
        for i in range (len (dict_b[k])):
          if dict_a[k][i] == dict_b[k][i]:
            print "-",
          else:
            print dict_b[k][i],
        print
    else:
      print change_type, "-", key_to_str (k)                 # entry deleted

  for k in dict_b:
    if k in dict_a:
      pass                          # attribute change <-> handled above
    else:
      print change_type, "+", key_to_str (k), key_to_str (dict_b[k])      # entry added

dict_a = dict_from_table ("links", 2, version_a)
print "======================================================================="
dict_b = dict_from_table ("links", 2, version_b)
print "======================================================================="
print_changes ("l", dict_a, dict_b)

dict_a = dict_from_table ("inodes", 1, version_a)
dict_b = dict_from_table ("inodes", 1, version_b)
print_changes ("i", dict_a, dict_b)
