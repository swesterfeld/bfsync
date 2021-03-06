# bfsync: Big File synchronization tool
# Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

from utils import *
from RemoteRepo import RemoteRepo
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

def expire (repo, args):
  reset_all_tags = False

  if len (args) == 1:
    if args[0] == "reset-all-tags":
      reset_all_tags = True
    else:
      raise BFSyncError ("expire: unsupported command line args")
  elif len (args) > 1:
    raise BFSyncError ("expire: unsupported command line args")

  # lock: makes filesystem reread history after expire
  lock = repo.try_lock()

  first_unused_version = repo.first_unused_version()

  if reset_all_tags:
    ## reset all tags that expire sets (doesn't undelete versions, though)

    repo.bdb.begin_transaction()
    for version in range (1, first_unused_version):
      values = repo.bdb.load_tag (version, "backup-type")
      for btype in values:
        repo.bdb.del_tag (version, "backup-type", btype)
    repo.bdb.commit_transaction()
    return

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

  def cfg_first (name):
    s = cfg_value (name)
    if s == "first":
      return True
    if s == "last":
      return False
    raise BFSyncError ("expire: expire/%s entry must be first or last" % name)

  def tag (version, tag):
    repo.bdb.add_tag (version, "backup-type", tag)

  def find_version_tag (versions, backup_type):
    for version in versions:
      values = repo.bdb.load_tag (version, "backup-type")
      if backup_type in values:
        return True
    return False

  def tag_best_version (versions, best_time, backup_type):
    if len (versions) == 0:   # empty set => no tagging
      return

    # don't tag if group already has a tag
    if find_version_tag (versions, backup_type):
      return

    best_delta = datetime.timedelta (days = 10000)

    for version in versions:
      he = repo.bdb.load_history_entry (version)
      he_datetime = datetime.datetime.fromtimestamp (he.time)
      he_delta = abs (best_time - he_datetime)
      if he_delta < best_delta:
        best_version = version
        best_delta = he_delta

    if best_version + 1 == first_unused_version:
      tag (best_version, "%s-candidate" % backup_type)
    else:
      tag (best_version, backup_type)

  def tag_first_last_version (versions, first, backup_type):
    if len (versions) == 0:   # empty set => no tagging
      return

    # don't tag if group already has a tag
    if find_version_tag (versions, backup_type):
      return

    if first:                 # take first or last in version list (configurable)
      best_version = versions[0]
    else:
      best_version = versions[-1]

    if best_version + 1 == first_unused_version:
      tag (best_version, "%s-candidate" % backup_type)
    else:
      tag (best_version, backup_type)

  repo.bdb.begin_transaction()
  keep = set()

  ## untag candidates

  for version in range (1, first_unused_version):
    values = repo.bdb.load_tag (version, "backup-type")
    for btype in [ "daily-candidate", "weekly-candidate", "monthly-candidate", "yearly-candidate" ]:
      if btype in values:
        repo.bdb.del_tag (version, "backup-type", btype)

  ## generate keep set for most recent versions

  keep_most_recent = cfg_number ("keep-most-recent")
  recent_start = first_unused_version - keep_most_recent
  recent_start = max (1, recent_start)

  for version in range (recent_start, first_unused_version):
    keep.add (version)

  ## group versions according to a given strftime format

  def group_versions (versions, ftime):
    group_dict = dict()
    for version in versions:
      he = repo.bdb.load_history_entry (version)
      he_datetime = datetime.datetime.fromtimestamp (he.time)
      group_str = he_datetime.strftime (ftime)
      if not group_dict.has_key (group_str):
        group_dict[group_str] = []
      group_dict[group_str].append (version)
    return group_dict

  ## tag backups (not weekly)

  def group_and_tag (versions, ftime, tag):
    create_first = cfg_first ("create-" + tag)
    group_dict = group_versions (versions, ftime)

    for group_str in group_dict:
      tag_first_last_version (group_dict[group_str], create_first, tag)

  ## tag daily backups

  group_and_tag (range (1, first_unused_version), "%Y%m%d", "daily")

  ## create list of daily backups

  daily_backups = []
  for version in range (1, first_unused_version):
    values = repo.bdb.load_tag (version, "backup-type")
    if "daily" in values or "daily-candidate" in values:
      daily_backups.append (version)

  ## tag weekly backups

  create_weekly = day2index (cfg_value ("create-weekly"))
  week_dict = group_versions (daily_backups, "%Y%W")

  for week_nr in week_dict:
    best_time = datetime.datetime.strptime ('%d %d %d' % (int (week_nr) / 100, int (week_nr) % 100, create_weekly), '%Y %W %w')
    best_time += datetime.timedelta (seconds = 23 * 3600 + 59 * 60)
    tag_best_version (week_dict[week_nr], best_time, "weekly")

  ## tag monthly/yearly backups

  group_and_tag (daily_backups, "%Y%m", "monthly")
  group_and_tag (daily_backups, "%Y", "yearly")

  repo.bdb.commit_transaction()

  def update_keep_set (btype):
    backup_list = []

    for version in range (1, first_unused_version):
      values = repo.bdb.load_tag (version, "backup-type")
      if btype in values:
        backup_list.append (version)
      if btype + "-candidate" in values:
        keep.add (version)

    number_of_backups_to_keep = cfg_number ("keep-" + btype)
    bstart = max (len (backup_list) - number_of_backups_to_keep, 0)
    for version in backup_list[bstart:]:
      keep.add (version)

  ## update keep set from daily/weekly/... tags

  update_keep_set ("daily")
  update_keep_set ("weekly")
  update_keep_set ("monthly")
  update_keep_set ("yearly")

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

def copy_expire (repo, url, rsh):
  if url is None:
    default_copy_expire = repo.config.get ("default/copy-expire")
    if len (default_copy_expire) == 0:
      raise BFSyncError ("copy-expire: no repository specified and default/copy-expire config value empty")
    url = default_copy_expire[0]

  remote_repo = RemoteRepo (url, rsh)
  remote_history = remote_repo.get_history()
  remote_tags = remote_repo.get_tags()

  repo.bdb.begin_transaction()

  for version_idx in range (len (remote_tags)):
    (version, hash) = remote_history[version_idx][0:2]
    hentry = repo.bdb.load_history_entry (version)
    if hentry.valid:
      assert (version == hentry.version and hash == hentry.hash)

      # delete old tags in local history
      for tag in repo.bdb.list_tags (hentry.version):
        if tag == "deleted" or tag == "backup-type":
          values = repo.bdb.load_tag (hentry.version, tag)
          for value in values:
            repo.bdb.del_tag (hentry.version, tag, value)

      # set new tags from remote_tags
      for tag_list in remote_tags[version_idx][1:]:
        (tag, values) = tag_list[0], tag_list[1:]
        if tag == "deleted" or tag == "backup-type":
          for value in values:
            repo.bdb.add_tag (version, tag, value)

  repo.bdb.commit_transaction()
