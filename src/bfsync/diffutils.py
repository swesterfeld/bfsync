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

def diff (repo, version_a, version_b, outfile):
  # write changes to outfile
  dg = bfsyncdb.DiffGenerator (repo.bdb, version_a, version_b)

  while True:
    change = dg.get_next()
    if len (change) == 0: # done?
      return

    write1change (change, outfile)
