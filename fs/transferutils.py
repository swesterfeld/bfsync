from RemoteRepo import RemoteRepo
from TransferList import TransferList, TransferFile
import os
import sys
from utils import *
from applyutils import apply
from commitutils import revert
import argparse
import subprocess
import datetime

def get_remote_objects (remote_repo, transfer_objs):
  # make a list of hashes that we need
  need_hash = dict()
  for thash in transfer_objs:
    dest_file = os.path.join ("objects", make_object_filename (thash))
    if not validate_object (dest_file, thash):
      need_hash[thash] = True

  # check for objects in remote repo
  remote_list = remote_repo.ls()
  tlist = TransferList()
  for rfile in remote_list:
    if need_hash.has_key (rfile.hash):
      src_file = os.path.join (remote_repo.path, "objects", make_object_filename (rfile.hash))
      dest_file = os.path.join ("objects", make_object_filename (rfile.hash))
      tlist.add (TransferFile (src_file, dest_file, rfile.size, 0400))

  # do the actual copying
  remote_repo.get_objects (tlist)

def get (repo, urls):
  conn = repo.conn
  repo_path = repo.path

  if len (urls) == 0:
    default_get = repo.config.get ("default/get")
    if len (default_get) == 0:
      raise Exception ("get: no repository specified and default/get config value empty")
    url = default_get[0]
  else:
    url = urls[0]

  remote_repo = RemoteRepo (url)

  c = conn.cursor()

  # create list of required objects
  objs = []
  c.execute ('''SELECT DISTINCT hash FROM inodes''')
  for row in c:
    s = "%s" % row[0]
    if len (s) == 40:
      objs += [ s ]

  get_remote_objects (remote_repo, objs)

def push (repo, urls):
  conn = repo.conn
  repo_path = repo.path

  if len (urls) == 0:
    default_push = repo.config.get ("default/push")
    if len (default_push) == 0:
      raise Exception ("push: no repository specified and default/push config value empty")
    url = default_push[0]
  else:
    url = urls[0]

  remote_repo = RemoteRepo (url)
  remote_history = remote_repo.get_history()

  c = conn.cursor()
  c.execute ('''SELECT * FROM history WHERE hash != '' ORDER BY version''')

  local_history = []
  for row in c:
    local_history += [ row ]

  common_version = 0
  for v in range (min (len (local_history), len (remote_history))):
    lh = local_history[v]
    rh = remote_history[v]
    # check version
    assert (lh[0] == v + 1)
    assert (rh[0] == v + 1)
    if lh[1] == rh[1]:
      common_version = v + 1
    else:
      break
  if common_version != len (remote_history):
    raise Exception ("push failed, remote history contains commits not in local history (pull to fix this)")

  delta_history = []
  for v in range (len (local_history)):
    if v + 1 > common_version:
      delta_history += [ local_history[v] ]

  print remote_repo.update_history (delta_history)

  need_objs = remote_repo.need_objects()

  tl = TransferList()
  for hash in need_objs:
    src_file = os.path.join ("objects", make_object_filename (hash))
    if validate_object (src_file, hash):
      tl.add (TransferFile (src_file, os.path.join (remote_repo.path, src_file), os.path.getsize (src_file), 0400))

  remote_repo.put_objects (tl)

def load_diff (hash):
  obj_name = os.path.join ("objects", make_object_filename (hash))
  diff = subprocess.Popen(["xzcat", obj_name], stdout=subprocess.PIPE).communicate()[0]
  return diff

################################# MERGE ######################################

def find_common_version (local_history, remote_history):
  common_version = 0
  for v in range (min (len (local_history), len (remote_history))):
    lh = local_history[v]
    rh = remote_history[v]
    # check version
    assert (lh[0] == v + 1)
    assert (rh[0] == v + 1)
    if lh[1] == rh[1]:
      common_version = v + 1
    else:
      break
  print "merge: last common version:", common_version
  return common_version

def db_link_inode (c, VERSION, dir_id, name):
  c.execute ("SELECT inode_id FROM links WHERE dir_id = ? AND name = ? AND ? >= vmin AND ? <= vmax",
             (dir_id, name, VERSION, VERSION))
  for row in c:
    return row[0]
  raise Exception ("link target for %s/%s not found" % (dir_id, name))

class MergeHistory:
  def __init__ (self, c, common_version):
    self.c = c
    self.common_version = common_version
    self.inode_changes = dict()

  def inode4change (self, change):
    if change[0] == "i+" or change[0] == "i-" or change[0] == "i!":
      return change[1]
    if change[0] == "l+":
      return change[3]
    if change[0] == "l-":
      # this one depends on the context, since the inode id it belongs to is not
      # stored within the change itself
      return db_link_inode (self.c, self.common_version, change[1], change[2])
    return "???"

  def add_changes (self, version, changes):
    for change in changes:
      print ".........", "|".join(change)
      inode = self.inode4change (change)

      if not self.inode_changes.has_key (inode):
        self.inode_changes[inode]  = []
      self.inode_changes[inode] += [ (version, change) ]

  def get_changes_without (self, version, ignore_inode_change):
    changes = []
    for inode in self.inode_changes:
      if not ignore_inode_change.has_key (inode):
        for (v, change) in self.inode_changes[inode]:
          if v == version:
            changes += [ change ]
    return changes

  def inodes (self):
    return self.inode_changes.keys()


def find_conflicts (mhistory, lhistory):
  mset = set (mhistory.inodes())
  lset = set (lhistory.inodes())
  return list (mset.intersection (lset))

def db_inode (c, VERSION, id):
  c.execute ("SELECT * FROM inodes WHERE id = ? AND ? >= vmin AND ? <= vmax",
             (id, VERSION, VERSION))
  for row in c:
    return list (row[2:])
  return False

def db_links (c, VERSION, id):
  c.execute ("SELECT dir_id, name, inode_id FROM links WHERE inode_id = ? AND ? >= vmin AND ? <= vmax",
             (id, VERSION, VERSION))
  results = []
  for row in c:
    results += [ list (row) ]
  return results

def restore_inode_links (want_links, have_links):
  changes = []

  # build dictionaries for links:

  want_dict = dict()
  for l in want_links:
    want_dict[(l[0], l[1])] = l

  have_dict = dict()
  for l in have_links:
    have_dict[(l[0], l[1])] = l

  for k in have_dict:
    if not want_dict.has_key (k):
      del_link = have_dict[k]
      changes += [ [ "l-", str (del_link[0]), str (del_link[1]) ] ]

  for k in want_dict:
    if not have_dict.has_key (k):
      add_link = want_dict[k]
      changes += [ [ "l+", str (add_link[0]), str (add_link[1]), str (add_link[2]) ] ]
  return changes


################################# MERGE ######################################

def history_merge (c, repo, local_history, remote_history, pull_args):
  # revert to last common version
  common_version = find_common_version (local_history, remote_history)
  revert (repo, common_version)

  # ANALYZE master history
  master_merge_history = MergeHistory (c, common_version)

  for rh in remote_history:   # remote history
    if rh[0] > common_version:
      diff = rh[1]
      changes = parse_diff (load_diff (diff))
      master_merge_history.add_changes (rh[0], changes)

  # ANALYZE local history
  local_merge_history = MergeHistory (c, common_version)

  for lh in local_history:    # local history
    if lh[0] > common_version:
      diff = lh[1]
      changes = parse_diff (load_diff (diff))
      local_merge_history.add_changes (lh[0], changes)

  # ASK USER
  inode_ignore_change = dict()
  restore_inode = dict()

  print
  conflicts = find_conflicts (master_merge_history, local_merge_history)
  for conflict in conflicts:
    if pull_args.always_local:
      choice = "l"
    elif pull_args.always_master:
      choice = "m"
    else:
      while True:
        print "CONFLICT (%s): %s" % (conflict, printable_name (c, conflict, common_version))
        print "(m)aster / (l)ocal ? ",
        line = raw_input ("How should this conflict be resolved? ")
        if line == "m" or line == "l":
          choice = line
          break
    if choice == "l":
      print "... local version will be used"
      restore_inode[conflict] = True
    elif choice == "m":
      print "... master version will be used"
      inode_ignore_change[conflict] = True

  print

  master_version = common_version

  # APPLY master history
  for rh in remote_history:
    if rh[0] > common_version:
      diff = rh[1]
      diff_file = os.path.join ("objects", make_object_filename (diff))

      print "applying patch %s" % diff
      os.system ("xzcat %s > tmp-diff" % diff_file)
      f = open ("tmp-diff", "r")
      apply (repo, f, diff)
      f.close()
      os.remove ("tmp-diff")
      master_version = rh[0]

  # APPLY extra commit to be able to apply local history without problems
  new_diff_filename = os.path.join (repo.path, "tmp-merge-diff")
  new_diff_file = open (new_diff_filename, "w")

  changes = []
  for inode in restore_inode:
    common_inode = db_inode (c, common_version, inode)
    if db_inode (c, master_version, inode):
      # inode still exists: just undo changes
      changes += [ map (str, [ "i!" ] + common_inode) ]
    else:
      # inode is gone: recreate it to allow applying local changes
      changes += [ map (str, [ "i+" ] + common_inode) ]
    changes += restore_inode_links (db_links (c, common_version, inode), db_links (c, master_version, inode))

  for change in changes:
    # write change to diff
    s = ""
    for change_field in change:
      s += change_field + "\0"
    new_diff_file.write (s)
    print "MERGED> ", "|".join (change)
  new_diff_file.close()

  print "applying patch tmp"
  new_diff_file = open ("tmp-merge-diff")
  apply (repo, new_diff_file)
  new_diff_file.close()

  # APPLY modified local history
  for lh in local_history:
    if lh[0] > common_version:
      # adapt diff to get rid of conflicts
      new_diff_filename = os.path.join (repo.path, "tmp-merge-diff")
      new_diff_file = open (new_diff_filename, "w")

      changes = local_merge_history.get_changes_without (lh[0], inode_ignore_change)

      for change in changes:
        # write change to diff
        s = ""
        for change_field in change:
          s += change_field + "\0"
        new_diff_file.write (s)
        print "MERGED> ", "|".join (change)
      new_diff_file.close()

      # apply modified diff
      print "applying patch tmp-merge-diff/%d" % lh[0]
      new_diff_file = open ("tmp-merge-diff")
      apply (repo, new_diff_file)
      new_diff_file.close()

def pull (repo, args, server = True):
  parser = argparse.ArgumentParser (prog='bfsync2 pull')
  parser.add_argument ('--always-local', action='store_const', const=True,
                       help='always use local version for merge conflicts')
  parser.add_argument ('--always-master', action='store_const', const=True,
                       help='always use master version for merge conflicts')
  parser.add_argument ('repo', nargs = '?')
  pull_args = parser.parse_args (args)

  conn = repo.conn
  repo_path = repo.path

  if pull_args.repo is None:
    default_pull = repo.config.get ("default/pull")
    if len (default_pull) == 0:
      raise Exception ("pull: no repository specified and default/push config value empty")
    url = default_pull[0]
  else:
    url = pull_args.repo

  remote_repo = RemoteRepo (url)
  remote_history = remote_repo.get_history()

  c = conn.cursor()
  c.execute ('''SELECT * FROM history WHERE hash != '' ORDER BY version''')

  local_history = []
  for row in c:
    local_history += [ row ]

  l_dict = dict()     # dict: version number -> diff hash
  for lh in local_history:
    version = lh[0]
    hash = lh[1]
    if hash:
      l_dict[version] = hash

  ff_apply = []
  transfer_objs = []
  can_fast_forward = True
  for rh in remote_history:
    version = rh[0]
    hash = rh[1]
    if hash:
      transfer_objs += [ hash ]
      if l_dict.has_key (version):
        if hash != l_dict[version]:
          can_fast_forward = False
        else:
          pass    # same version, local and remote
      else:
        ff_apply += [ hash ]

  # transfer required history objects
  get_remote_objects (remote_repo, transfer_objs)

  if can_fast_forward:
    print "will fast-forward %d versions..." % len (ff_apply)
    for diff in ff_apply:
      diff_file = os.path.join ("objects", make_object_filename (diff))
      print "applying patch %s" % diff
      os.system ("xzcat %s > tmp-diff" % diff_file)
      f = open ("tmp-diff", "r")
      apply (repo, f, diff, server = server)
      f.close()
      os.remove ("tmp-diff")
  else:
    history_merge (c, repo, local_history, remote_history, pull_args)
