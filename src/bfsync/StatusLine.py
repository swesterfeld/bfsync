import sys

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
