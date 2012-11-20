#!/usr/bin/python

import bfsyncdb

hcdict = bfsyncdb.HashCacheDict()
hcdict.insert ("42" * 20, "ff" * 20, 12345)

K = "5051d8567ca5234b2e24bbf502b26e9497686d9d"
hcdict.insert ("15e915d9dface829a404ae80b8acdce4cfcdd8fb", K, 123454321)

e = hcdict.lookup ("ff" * 20)
print "expect true: ", e.valid, e.stat_hash, e.file_hash, e.expire_time

e = hcdict.lookup (K)
print "expect true: ", e.valid, e.stat_hash, e.file_hash, e.expire_time

e = hcdict.lookup ("bf" * 20)
print "expect false: ", e.valid
