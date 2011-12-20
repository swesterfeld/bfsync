#!/usr/bin/python

# bfsync: Big File synchronization tool

# Copyright (C) 2011 Stefan Westerfeld
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

import time
import os
import lzma

# the manpage says that levels > "-6" only improve compression ratio if the file
# is bigger than 8M

compressor6 = None
compressor9 = None

def xz_compressor_for_level (level):
  global compressor6, compressor9

  if (level == 6):
    if compressor6 is None:
      compressor6 = lzma.LZMACompressor ({'format':'xz', 'level':6})
    return compressor6
  else:
    if compressor9 is None:
      compressor9 = lzma.LZMACompressor ({'format':'xz', 'level':9})
    return compressor9

def xz (filename, level, inproc):
  if not inproc:
    os.system ("xz -%d %s" % (level, filename))
    return
  compressor = xz_compressor_for_level (level)

  f = open (filename, "r")
  xz_f = open ("%s.xz" % filename, "w")

  xz_f.write (compressor.compress (f.read()))
  xz_f.write (compressor.flush())

  xz_f.close()
  f.close()
  os.remove (filename)

  compressor.reset()

for level in [6, 9]:
  for inproc in [ False, True ]:
    t = time.time()
    nfiles = 0

    while (time.time() - t) < 10:
      f = open ("/tmp/tmpfs/x", "w")
      f.write ("temp%03d\n" % nfiles)
      f.close()

      xz ("/tmp/tmpfs/x", level, inproc)
      os.remove ("/tmp/tmpfs/x.xz")
      nfiles += 1

    print "%.2f ms (level=%d, inproc=%s)" % ((1000 * (time.time() - t) / nfiles), level, inproc)
