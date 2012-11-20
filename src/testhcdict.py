#!/usr/bin/python

import bfsyncdb

hcdict = bfsyncdb.HashCacheDict()
hcdict.insert ("ff" * 20, "42" * 20, 12345)

K = "5051d8567ca5234b2e24bbf502b26e9497686d9d"
V = "15e915d9dface829a404ae80b8acdce4cfcdd8fb"
T = 123454321
hcdict.insert (K, V, T)

e = hcdict.lookup ("ff" * 20)
assert (e.valid == True)
print "expect true: ", e.valid, e.stat_hash, e.file_hash, e.expire_time

e = hcdict.lookup (K)
print "expect true: ", e.valid, e.stat_hash, e.file_hash, e.expire_time
assert (e.valid == True)
assert (e.stat_hash == K)
assert (e.file_hash == V)
assert (e.expire_time == T)

e = hcdict.lookup ("bf" * 20)
assert (e.valid == False)
print "expect false: ", e.valid

hci = bfsyncdb.HashCacheIterator (hcdict)

while True:
  e = hci.get_next()
  if not e.valid:
    break
  print e.stat_hash, "=", e.file_hash, e.expire_time
