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

import lzma
import os

compressor = None

def xz (filename):
  global compressor

  opts = {'format':'xz', 'level':6}
  if compressor is None:
    compressor = lzma.LZMACompressor (opts)
  else:
    compressor.reset (opts)

  f = open (filename, "r")
  xz_f = open ("%s.xz" % filename, "w")

  # compress data block-by-block until eof
  while True:
    data = f.read (256 * 1024)
    if (len (data) == 0):
      break
    xz_f.write (compressor.compress (data))

  # write remaining compressed data
  xz_f.write (compressor.flush())

  xz_f.close()
  f.close()
  os.remove (filename)

def xzcat (filename):
  f = open (filename, "r")
  xz_data = f.read()
  f.close()

  return lzma.decompress (xz_data)

decompressor = None

def xzcat2 (in_filename, out_filename):
  global decompressor

  if decompressor is None:
    decompressor = lzma.LZMADecompressor()
  else:
    decompressor.reset()

  f = open (in_filename, "r")
  uncompressed_f = open (out_filename, "w")

  # decompress data block-by-block until eof
  while True:
    xz_data = f.read (256 * 1024)
    if (len (xz_data) == 0):
      break

    uncompressed_f.write (decompressor.decompress (xz_data))

  # write remaining compressed data
  uncompressed_f.write (decompressor.flush())

  f.close()
  uncompressed_f.close()
