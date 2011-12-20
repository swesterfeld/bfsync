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

# the manpage says that levels > "-6" only improve compression ratio if the file
# is bigger than 8M

import lzma
import os

compressor6 = None
compressor9 = None

def xz_compressor_for_file (filename):
  global compressor6, compressor9

  size = os.path.getsize (filename)
  if (size <= 8 * 1024 * 1024):
    if compressor6 is None:
      compressor6 = lzma.LZMACompressor ({'format':'xz', 'level':6})
    return compressor6
  else:
    if compressor9 is None:
      compressor9 = lzma.LZMACompressor ({'format':'xz', 'level':9})
    return compressor9

def xz (filename):
  compressor = xz_compressor_for_file (filename)

  f = open (filename, "r")
  xz_f = open ("%s.xz" % filename, "w")

  xz_f.write (compressor.compress (f.read()))
  xz_f.write (compressor.flush())

  xz_f.close()
  f.close()
  os.remove (filename)

  compressor.reset()

def xzcat (filename):
  f = open (filename, "r")
  xz_data = f.read()
  f.close()

  return lzma.decompress (xz_data)
