# bfsync: Big File synchronization tool

# Copyright (C) 2012 Stefan Westerfeld
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

from utils import *
import re

def expire (repo):
  # lock: makes filesystem reread history after expire
  lock = repo.try_lock()

  def cfg_number (name):
    xlist = repo.config.get ("expire/%s" % name)
    if len (xlist) != 1:
      raise BFSyncError ("expire: need exactly one expire/%s entry" % name)
    m = re.match ("^([0-9]+)$", xlist[0])
    if not m:
      raise BFSyncError ("expire/%s: needs to be a number" % name)
    return int (xlist[0])

  keep = set()
  keep_most_recent = cfg_number ("keep_most_recent")

  first_unused_version = repo.first_unused_version()

  recent_start = first_unused_version - keep_most_recent
  recent_start = max (1, recent_start)

  for version in range (recent_start, first_unused_version):
    keep.add (version)

  ## delete all versions not in keep set
  repo.bdb.begin_transaction()

  count = 0
  for version in range (1, first_unused_version):
    if version not in keep:
      if "deleted" not in repo.bdb.list_tags (version):
        count += 1
        repo.bdb.add_tag (version, "deleted", "1")

  repo.bdb.commit_transaction()
  print "EXPIRE: %d versions deleted during expire" % count
