# bfsync: Big File synchronization tool
# Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

import time
import sys

# helper class to update output only once per second
class OutputSubsampler:
  def __init__ (self):
    self.last_update = 0

  def need_update (self):
    update_time = time.time()
    if update_time - self.last_update > 1:
      self.last_update = update_time
      return True
    else:
      return False

class StatusLine:
  def __init__ (self):
    self.op_text = ""
    self.line = ""
  def set_op (self, op):
    self.op_text = op + ": "
  def update (self, text):
    print "\r",
    # clear old status
    for i in range (len (self.line)):
      sys.stdout.write (" ")
    self.line = self.op_text + text
    print "\r%s " % self.line,
    sys.stdout.flush()
  def cleanup (self):
    if len (self.line) > 0:
      print
    self.line = ""
  def die (self, text):
    self.cleanup()
    sys.stderr.write ("bfsync: %s\n" % text)
    sys.exit (1)

status_line = StatusLine()
