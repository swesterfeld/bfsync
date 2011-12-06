from RemoteRepo import RemoteRepo
from TransferList import TransferList, TransferFile
import os
import sys
from utils import *
from applyutils import apply
from commitutils import revert
from xzutils import xzcat
from StatusLine import status_line
import argparse
import subprocess
import datetime
import random

def get_remote_objects (repo, remote_repo, transfer_objs):
  # make a list of hashes that we need
  need_hash = dict()
  need_hash_list = []
  for thash in transfer_objs:
    if not need_hash.has_key (thash):
      dest_file = os.path.join ("objects", make_object_filename (thash))
      if not validate_object (dest_file, thash):
        need_hash[thash] = True
        need_hash_list.append (thash)

  # check for objects in remote repo
  remote_list = remote_repo.ls (need_hash_list)
  tlist = TransferList()
  for rfile in remote_list:
    if need_hash.has_key (rfile.hash):
      tlist.add (TransferFile (rfile.hash, rfile.size))

  # do the actual copying
  remote_repo.get_objects (repo, tlist)

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

  get_remote_objects (repo, remote_repo, objs)

def put (repo, urls):
  conn = repo.conn
  repo_path = repo.path

  if len (urls) == 0:
    default_get = repo.config.get ("default/put")
    if len (default_get) == 0:
      raise Exception ("put: no repository specified and default/put config value empty")
    url = default_get[0]
  else:
    url = urls[0]

  remote_repo = RemoteRepo (url)
  need_objs = remote_repo.need_objects ("inodes")

  tl = TransferList()
  for hash in need_objs:
    src_file = os.path.join ("objects", make_object_filename (hash))
    if validate_object (src_file, hash):
      tl.add (TransferFile (hash, os.path.getsize (src_file)))

  remote_repo.put_objects (repo, tl)

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

  remote_repo.update_history (delta_history)

  need_objs = remote_repo.need_objects ("history")

  if len (delta_history) == 0 and len (need_objs) == 0:
    print "Everything up-to-date."
    return

  tl = TransferList()
  for hash in need_objs:
    src_file = os.path.join ("objects", make_object_filename (hash))
    if validate_object (src_file, hash):
      tl.add (TransferFile (hash, os.path.getsize (src_file)))

  remote_repo.put_objects (repo, tl)

def load_diff (hash):
  obj_name = os.path.join ("objects", make_object_filename (hash))
  diff = xzcat (obj_name)
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
  return common_version

def db_link_inode (c, VERSION, dir_id, name):
  c.execute ("SELECT inode_id FROM links WHERE dir_id = ? AND name = ? AND ? >= vmin AND ? <= vmax",
             (dir_id, name, VERSION, VERSION))
  for row in c:
    return row[0]
  raise Exception ("link target for %s/%s not found" % (dir_id, name))

class MergeHistory:
  def __init__ (self, c, common_version, name):
    self.c = c
    self.common_version = common_version
    self.inode_changes = dict()
    self.name = name
    self.new_links = dict()
    self.position = 1               # for preserving change order

  def inode4change (self, change):
    if change[0] == "i+" or change[0] == "i-" or change[0] == "i!":
      return change[1]
    if change[0] == "l+":
      self.new_links[(change[1], change[2])] = change[3]
      return change[3]
    if change[0] == "l-":
      # this one depends on the context, since the inode id it belongs to is not
      # stored within the change itself
      if self.new_links.has_key ((change[1], change[2])):
        return self.new_links [(change[1], change[2])]
      else:
        return db_link_inode (self.c, self.common_version, change[1], change[2])
    return "???"

  def add_1_change (self, version, change):
    inode = self.inode4change (change)

    if not self.inode_changes.has_key (inode):
      self.inode_changes[inode]  = []
    self.inode_changes[inode] += [ (self.position, version, change) ]
    self.position += 1

  def add_changes (self, version, changes):
    for change in changes:
      if change[0] == "l!":
        change1 = [ "l-", change[1], change[2] ]
        change2 = [ "l+", change[1], change[2], change[3] ]
        self.add_1_change (version, change1)
        self.add_1_change (version, change2)
      else:
        self.add_1_change (version, change)

  def get_changes_without (self, version, ignore_inode_change):
    pchanges = []
    for inode in self.inode_changes:
      if not ignore_inode_change.has_key (inode):
        for (position, v, change) in self.inode_changes[inode]:
          if v == version:
            pchanges += [ (position, change) ]

    # preserve change order
    pchanges.sort (key = lambda x: x[0])
    changes = map (lambda x: x[1], pchanges)
    return changes

  def show_changes (self, inode):
    changes = []
    for (position, v, change) in self.inode_changes[inode]:
      print v, "|".join (change)
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

def db_contains_link (c, VERSION, dir_id, name):
  c.execute ("SELECT * FROM links WHERE dir_id = ? AND name = ? AND ? >= vmin AND ? <= vmax",
             (dir_id, name, VERSION, VERSION))
  for row in c:
    return True
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

def gen_id():
  id = ""
  for i in range (5):
    id += "%08x" % random.randint (0, 2**32 - 1)
  return id

class DiffRewriter:
  def __init__ (self, c):
    self.c = c
    self.link_rewrite = dict()
    self.subst = dict()

  def subst_inode (self, old_id, new_id):
    self.subst[old_id] = new_id

  def rewrite (self, changes):
    # determine current db version
    VERSION = 1
    self.c.execute ('''SELECT version FROM history''')
    for row in self.c:
      VERSION = max (row[0], VERSION)

    new_diff = ""
    for change in changes:
      if change[0] == "l+":
        if self.subst.has_key (change[3]):
          change[3] = self.subst[change[3]]
        if self.subst.has_key (change[1]):
          change[1] = self.subst[change[1]]
        if db_contains_link (self.c, VERSION, change[1], change[2]):
          print "LINK CONFLICT"
          suffix = 1
          while db_contains_link (self.c, VERSION, change[1], change[2] + "~%d" % suffix):
            suffix += 1
          lrkey = (change[1], change[2])
          self.link_rewrite[lrkey] = change[2] + "~%d" % suffix
      if change[0] == "l+" or change[0] == "l-":
        if self.subst.has_key (change[1]):
          change[1] = self.subst[change[1]]
        lrkey = (change[1], change[2])
        if self.link_rewrite.has_key (lrkey):
          change[2] = self.link_rewrite[lrkey]
      if change[0] == "l!":
        raise Exception ("unable to deal with l! changes during merge/local history apply")
      if change[0] == "i!" or change[0] == "i-":
        if self.subst.has_key (change[1]):
          change[1] = self.subst[change[1]]

      # write change to diff
      s = ""
      for change_field in change:
        s += change_field + "\0"
      new_diff += s

    return new_diff

################################# MERGE ######################################

def history_merge (c, repo, local_history, remote_history, pull_args):
  # revert to last common version
  common_version = find_common_version (local_history, remote_history)
  revert (repo, common_version)

  # ANALYZE master history
  master_merge_history = MergeHistory (c, common_version, "master")

  for rh in remote_history:   # remote history
    if rh[0] > common_version:
      diff = rh[1]
      changes = parse_diff (load_diff (diff))
      master_merge_history.add_changes (rh[0], changes)

  # ANALYZE local history
  local_merge_history = MergeHistory (c, common_version, "local")

  for lh in local_history:    # local history
    if lh[0] > common_version:
      diff = lh[1]
      changes = parse_diff (load_diff (diff))
      local_merge_history.add_changes (lh[0], changes)

  # ASK USER
  inode_ignore_change = dict()
  restore_inode = dict()
  use_both_versions = dict()

  print
  conflicts = find_conflicts (master_merge_history, local_merge_history)
  for conflict in conflicts:
    if pull_args.always_local:
      choice = "l"
    elif pull_args.always_master:
      choice = "m"
    elif pull_args.always_both:
      if conflict == "0"*40:
        choice = "m"
      else:
        choice = "b"
    else:
      while True:
        print "CONFLICT (%s): %s" % (conflict, printable_name (c, conflict, common_version))
        print "(m)aster / (l)ocal / (b)oth / (s)how ? ",
        line = raw_input ("How should this conflict be resolved? ")
        if line == "s":
          print "=== MASTER ==="
          master_merge_history.show_changes (conflict)
          print "=== LOCAL ==="
          local_merge_history.show_changes (conflict)
          print "=============="
        if line == "m" or line == "l" or line == "b":
          choice = line
          break
    if choice == "l":
      print "... local version will be used"
      restore_inode[conflict] = True
    elif choice == "m":
      print "... master version will be used"
      inode_ignore_change[conflict] = True
    elif choice == "b":
      print "... both versions will be used"
      use_both_versions[conflict] = True

  print

  master_version = common_version

  # APPLY master history
  for rh in remote_history:
    if rh[0] > common_version:
      diff = rh[1]
      diff_file = os.path.join ("objects", make_object_filename (diff))

      print "applying patch %s" % diff
      apply (repo, xzcat (diff_file), diff)
      master_version = rh[0]

  # APPLY extra commit to be able to apply local history without problems
  diff_rewriter = DiffRewriter (c)

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

  for inode in use_both_versions:
    # duplicate inode
    common_inode = db_inode (c, common_version, inode)
    new_id = gen_id()
    changes += [ map (str, [ "i+", new_id ] + common_inode[1:]) ]
    diff_rewriter.subst_inode (common_inode[0], new_id)

    # duplicate inode links
    for link in db_links (c, common_version, inode):
      changes.append ([ "l+", link[0], link[1], new_id ])

  new_diff = diff_rewriter.rewrite (changes)

  if new_diff != "":
    print "applying patch tmp"
    apply (repo, new_diff)

  # APPLY modified local history

  for lh in local_history:
    if lh[0] > common_version:
      # adapt diff to get rid of conflicts
      changes = local_merge_history.get_changes_without (lh[0], inode_ignore_change)

      new_diff = diff_rewriter.rewrite (changes)
      # apply modified diff
      print "applying patch tmp-merge-diff/%d" % lh[0]
      apply (repo, new_diff)

def pull (repo, args, server = True):
  parser = argparse.ArgumentParser (prog='bfsync2 pull')
  parser.add_argument ('--always-local', action='store_const', const=True,
                       help='always use local version for merge conflicts')
  parser.add_argument ('--always-master', action='store_const', const=True,
                       help='always use master version for merge conflicts')
  parser.add_argument ('--always-both', action='store_const', const=True,
                       help='always use both versions for merge conflicts')
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

  # Already up-to-date?
  if can_fast_forward and len (ff_apply) == 0:
    print "Already up-to-date."
    return

  # transfer required history objects
  get_remote_objects (repo, remote_repo, transfer_objs)

  if can_fast_forward:
    status_line.set_op ("PULL")
    count = 1
    for diff in ff_apply:
      diff_file = os.path.join ("objects", make_object_filename (diff))
      status_line.update ("fast-forward: patch %d / %d" % (count, len (ff_apply)))
      apply (repo, xzcat (diff_file), diff, server = server, verbose = False)
      count += 1
  else:
    history_merge (c, repo, local_history, remote_history, pull_args)
