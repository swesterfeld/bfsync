#!/usr/bin/python

import distutils.util
import sys

vinf = sys.version_info

print "build/lib.%s-%d.%d" % (distutils.util.get_platform(), vinf.major, vinf.minor)
