#!/usr/bin/python

import bfsyncdb

hcdict = bfsyncdb.HashCacheDict()
hcdict.insert ("42" * 20, "ff" * 20, 12345)
