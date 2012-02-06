#!/usr/bin/python

import bfsyncdb
import sys
import hashlib
import time

bdb = bfsyncdb.open_db (sys.argv[1], 256, False)
for j in range (1, 10000):
  start = time.time()
  bdb.begin_transaction()
  for n in range (1, 20000):
    hash = hashlib.sha1 ("%x:%x" % (j, n)).hexdigest()
    x = bdb.load_hash2file (hash)
    if x != 0:
      raise Exception ("hash collision")
    bdb.store_hash2file (hash, n)
  bdb.commit_transaction()
  now = time.time()
  print "%d %.2f" % (j * 20000, now - start)
  sys.stdout.flush()
