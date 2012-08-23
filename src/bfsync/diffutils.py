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

import bfsyncdb

def write1change (change_list, outfile):
  for s in change_list:
    outfile.write (s + "\0")

def diff (repo, outfile):
  # write changes to outfile
  dg = bfsyncdb.DiffGenerator (repo.bdb)

  while True:
    change = dg.get_next()
    if len (change) == 0: # done?
      return

    write1change (change, outfile)

# parser to read changes from \0 seperated diff file
class DiffIterator:
  def __init__ (self, diff_file):
    self.diff_file = diff_file
    self.start = 0
    self.data = ""

  def seek (self, pos):
    self.data = ""
    self.start = 0
    self.diff_file.seek (0)
    for i in xrange (0, pos):
      change = self.next()
      if change is None:
        raise Exception ("DiffIterator: seek to position %d failed" % pos)

  def next_field (self):
    while True:
      end = self.data.find ("\0", self.start)
      if end == -1:
        new_data = self.diff_file.read (64 * 1024)
        if len (new_data) == 0: # eof
          return None
        else:
          self.data = self.data[self.start:] + new_data
          self.start = 0
      else:
        result = self.data[self.start:end]
        self.start = end + 1
        return result

  def next (self):
    change_type = self.next_field()
    if change_type is None:
      return None

    fcount = 0

    if change_type == "l+" or change_type == "l!":
      fcount = 4
    elif change_type == "l-":
      fcount = 3
    elif change_type == "i+" or change_type == "i!":
      fcount = 16
    elif change_type == "i-":
      fcount = 2

    assert (fcount != 0)

    result = [ change_type ]
    for i in range (fcount - 1):
      result.append (self.next_field())
    return result
