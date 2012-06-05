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
import datetime
import time

def day2index (day_str):
  count = 0
  # number as in strptime %w
  for day in [ "sunday", "monday", "tuesday", "wednesday", "thusday", "friday", "saturday" ]:
    if day_str == day:
      return count
    count += 1
  raise BFSyncError ("expire: create_weekly day name not supported")

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

  def cfg_value (name):
    xlist = repo.config.get ("expire/%s" % name)
    if len (xlist) != 1:
      raise BFSyncError ("expire: need exactly one expire/%s entry" % name)
    return xlist[0]

  def tag (version, tag):
    repo.bdb.add_tag (version, "backup-type", tag)

  repo.bdb.begin_transaction()
  keep = set()

  first_unused_version = repo.first_unused_version()

  ## generate keep set for most recent versions

  keep_most_recent = cfg_number ("keep_most_recent")
  recent_start = first_unused_version - keep_most_recent
  recent_start = max (1, recent_start)

  for version in range (recent_start, first_unused_version):
    keep.add (version)

  ## tag weekly backups

  create_weekly = day2index (cfg_value ("create_weekly"))
  week_dict = dict()
  for version in range (1, first_unused_version):
    he = repo.bdb.load_history_entry (version)
    he_datetime = datetime.datetime.fromtimestamp (he.time)
    print "%2d" % version, he_datetime.strftime ("%F"), "%20s" % he_datetime.strftime ("%A %H:%M:%S")

    week_nr = he_datetime.strftime ("%Y%W")
    if not week_dict.has_key (week_nr):
      week_dict[week_nr] = []
    week_dict[week_nr].append (version)

  for week_nr in week_dict:
    print
    print "------------------------------------------------------------"
    print week_nr, create_weekly
    print "------------------------------------------------------------"
    best_time = datetime.datetime.strptime ('%d %d %d' % (int (week_nr) / 100, int (week_nr) % 100, create_weekly), '%Y %W %w')
    best_time += datetime.timedelta (seconds = 23 * 3600 + 59 * 60)
    best_version = None
    best_delta = datetime.timedelta (days = 10000)
    print best_time.strftime ("%F %A %H:%M:%S")
    for version in week_dict[week_nr]:
      he = repo.bdb.load_history_entry (version)
      he_datetime = datetime.datetime.fromtimestamp (he.time)
      he_delta = abs (best_time - he_datetime)
      if he_delta < best_delta:
        best_version = version
        best_delta = he_delta
      print version, he_datetime.strftime ("%F"), "%20s" % he_datetime.strftime ("%A %H:%M:%S")
    if best_version + 1 == first_unused_version:
      tag (best_version, "weekly-candidate")
    else:
      tag (best_version, "weekly")

  repo.bdb.commit_transaction()

  ## update keep set from weekly tags

  keep_weekly = cfg_number ("keep_weekly")
  weekly_list = []
  for version in range (1, first_unused_version):
    values = repo.bdb.load_tag (version, "backup-type")
    if "weekly" in values:
      weekly_list.append (version)
    if "weekly-candidate" in values:
      keep.add (version)

  for version in weekly_list[-keep_weekly:]:
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
