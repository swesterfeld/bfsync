import lzma
import os

# the manpage says that levels > "-6" only improve compression ratio if the file
# is bigger than 8M

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
  #os.system ("xz -6 %s" % filename)
  #return
  compressor = xz_compressor_for_file (filename)

  f = open (filename, "r")
  xz_f = open ("%s.xz" % filename, "w")

  xz_f.write (compressor.compress (f.read()))
  xz_f.write (compressor.flush())

  xz_f.close()
  f.close()
  os.remove (filename)

  compressor.reset()
