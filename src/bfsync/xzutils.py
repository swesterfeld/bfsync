# bfsync: Big File synchronization tool
# Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

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
