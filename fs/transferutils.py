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
import shutil

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
  status_line.update ("preparing transfer list...")
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

  def get_changes (self, inode):
    changes = []
    for (position, v, change) in self.inode_changes[inode]:
      changes += [ change ]
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
    self.changes = []

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
          filename = change[2]
          base, ext = os.path.splitext (filename)

          suffix = 1
          while True:
            # foo.gif => foo~1.gif
            newname = base + "~%d" % suffix + ext
            if not db_contains_link (self.c, VERSION, change[1], newname):
              break
            suffix += 1

          lrkey = (change[1], change[2])
          self.link_rewrite[lrkey] = newname
          path = printable_name (self.c, change[1], VERSION)
          self.changes += [ (os.path.join (path, filename), os.path.join (path, newname)) ]

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

  def show_changes (self):
    if len (self.changes) > 0:
      status_line.cleanup()
      print
      print "The following local files were renamed to avoid name conflicts"
      print "with the master history:"
      print
      for old, new in self.changes:
        print " * '%s' => '%s'" % (old, new)
      print

def apply_inode_changes (inode, changes):
  inode = inode[:]  # copy inode
  for change in changes:
    if change[0] == "i!":
      assert (change[1] == inode[0])
      for i in range (len (inode)):
        new_field = change[i + 1]
        if new_field != '':
          if isinstance (inode[i], int):
            inode[i] = int (new_field)
          else:
            inode[i] = new_field
    if change[0] == "i-":
      assert (change[1] == inode[0])
      inode = None
  return inode

def pretty_date (sec, nsec):
  return datetime.datetime.fromtimestamp (sec).strftime ("%F %H:%M:%S.") + "%d" % (nsec / 100 / 1000 / 1000)

INODE_TYPE = 4
INODE_CONTENT = 5

INODE_CTIME = 11
INODE_CTIME_NS = 12
INODE_MTIME = 13
INODE_MTIME_NS = 14

def pretty_format (inode):
  pp = []
  pp += [ ("User", inode[1]) ]
  pp += [ ("Group", inode[2]) ]
  pp += [ ("Mode", "%o" % (inode[3] & 07777)) ]
  pp += [ ("Type", inode[INODE_TYPE]) ]
  pp += [ ("Content", inode[INODE_CONTENT]) ]
  pp += [ ("Symlink", inode[6]) ]
  pp += [ ("Size", inode[7]) ]
  pp += [ ("Major", inode[8]) ]
  pp += [ ("Minor", inode[9]) ]
  pp += [ ("NLink", inode[10]) ]
  pp += [ ("CTime", pretty_date (inode[11], inode[12])) ]
  pp += [ ("MTime", pretty_date (inode[13], inode[14])) ]
  return pp

def pretty_print_conflict (common_inode, master_inode, local_inode):
  c_fmt = pretty_format (common_inode)
  if master_inode:
    m_fmt = pretty_format (master_inode)
  if local_inode:
    l_fmt = pretty_format (local_inode)

  print "%-8s| %-22s| %-22s| %-22s" % ("", "Common", "Master", "Local")
  print "--------|-----------------------|-----------------------|----------------------"
  for i in range (len (c_fmt)):
    c = c_fmt[i][1]
    m = m_fmt[i][1]
    l = l_fmt[i][1]
    if c_fmt[i][0] == "Content":
      c = "old content"
      if m_fmt[i][1] != c_fmt[i][1]:
        m = "new content 1"
      else:
        m = c
      if l_fmt[i][1] != c_fmt[i][1]:
        if l_fmt[i][1] != m_fmt[i][1]:
          l = "new content 2"
        else:
          l = m
      else:
        l = c
    if l == c:
      l = "~"
    if m == c:
      m = "~"
    print "%-8s| %-22s| %-22s| %-22s" % (c_fmt[i][0], c, m, l)

class AutoConflictResolver:
  def __init__ (self, c, repo, common_version, master_merge_history, local_merge_history):
    self.c = c
    self.repo = repo
    self.common_version = common_version
    self.master_merge_history = master_merge_history
    self.local_merge_history = local_merge_history

  def auto_merge_by_ctime (self, master_inode, local_inode):
    m_sec = master_inode[INODE_CTIME]
    l_sec = local_inode[INODE_CTIME]

    if m_sec > l_sec:
      return "m"
    if l_sec > m_sec:
      return "l"

    # m_sec == l_sec
    m_nsec = master_inode[INODE_CTIME_NS]
    l_nsec = local_inode[INODE_CTIME_NS]
    if m_nsec > l_nsec:
      return "m"
    if l_nsec > m_nsec:
      return "l"

    # same ctime
    return "m"

  def resolve (self, conflict):
    common_inode = db_inode (self.c, self.common_version, conflict)
    master_inode = apply_inode_changes (common_inode, self.master_merge_history.get_changes (conflict))
    local_inode = apply_inode_changes (common_inode, self.local_merge_history.get_changes (conflict))

    if master_inode is None or local_inode is None:
      return ""  # deletion, can't do that automatically

    cfields = set()
    for i in range (len (common_inode)):
      if common_inode[i] != master_inode[i] or common_inode[i] != local_inode[i]:
        cfields.add (i)

    if cfields.issubset ([INODE_CTIME, INODE_CTIME_NS,
                          INODE_MTIME, INODE_MTIME_NS]):
      return self.auto_merge_by_ctime (master_inode, local_inode)
    else:
      return ""   # could not resolve automatically

class UserConflictResolver:
  def __init__ (self, c, repo, common_version, master_merge_history, local_merge_history):
    self.c = c
    self.repo = repo
    self.common_version = common_version
    self.master_merge_history = master_merge_history
    self.local_merge_history = local_merge_history

  def shell (self, filename, common_hash, master_hash, local_hash):
    old_cwd = os.getcwd()

    os.mkdir ("merge")
    os.chdir ("merge")

    common_file = os.path.join (old_cwd, "objects", make_object_filename (common_hash))
    shutil.copyfile (common_file, "common_%s" % filename)

    if master_hash:
      master_file = os.path.join (old_cwd, "objects", make_object_filename (master_hash))
      default_get = self.repo.config.get ("default/get")

      if len (default_get) == 0:
        raise Exception ("get: no repository specified and default/get config value empty")
      url = default_get[0]

      remote_repo = RemoteRepo (url)
      get_remote_objects (self.repo, remote_repo, [ master_hash ])

      shutil.copyfile (master_file, "master_%s" % filename)

    if local_hash:
      local_file = os.path.join (old_cwd, "objects", make_object_filename (local_hash))
      shutil.copyfile (local_file, "local_%s" % filename)

    os.system (os.environ['SHELL'])
    try:
      os.remove ("common_%s" % filename)
      os.remove ("master_%s" % filename)
      os.remove ("local_%s" % filename)
    except:
      pass
    os.chdir (old_cwd)
    os.rmdir ("merge")

  def resolve (self, conflict):
    while True:
      common_inode = db_inode (self.c, self.common_version, conflict)
      master_inode = apply_inode_changes (common_inode, self.master_merge_history.get_changes (conflict))
      local_inode = apply_inode_changes (common_inode, self.local_merge_history.get_changes (conflict))

      fullname = printable_name (self.c, conflict, self.common_version)
      print "=" * 80
      print "Merge Conflict: '%s' was" % fullname
      if master_inode is None:
        print " - deleted in master history"
      else:
        print " - modified in master history"
      if local_inode is None:
        print " - deleted locally"
      else:
        print " - modified locally"
      print "=" * 80
      pretty_print_conflict (common_inode, master_inode, local_inode)
      print "=" * 80
      line = raw_input ("(m)aster / (l)ocal / (b)oth / (v)iew / (s)hell / (a)bort merge? ")
      if line == "v":
        print "=== MASTER ==="
        self.master_merge_history.show_changes (conflict)
        print "=== LOCAL ==="
        self.local_merge_history.show_changes (conflict)
        print "=============="
      if line == "s":
        conflict_type = common_inode[INODE_TYPE]
        if conflict_type != "file":
          print
          print "Sorry, shell is only supported for plain files, but conflict type is %s." % conflict_type
          print
        else:
          c_hash = common_inode[INODE_CONTENT]
          if master_inode:
            m_hash = master_inode[INODE_CONTENT]
          else:
            m_hash = None
          if local_inode:
            l_hash = local_inode[INODE_CONTENT]
          else:
            l_hash = None
          self.shell (os.path.basename (fullname), c_hash, m_hash, l_hash)
      if line == "m" or line == "l" or line == "b" or line == "a":
        if line == "l":
          print "... local version will be used"
        elif line == "m":
          print "... master version will be used"
        elif line == "b":
          print "... both versions will be used"
        elif line == "a":
          print "... aborting merge"
        return line

################################# MERGE ######################################

def history_merge (c, repo, local_history, remote_history, pull_args):
  # figure out last common version
  common_version = find_common_version (local_history, remote_history)

  # initialize as one because of the middle-patch (which is neither from local nor remote history)
  total_patch_count = 1

  # ANALYZE master history
  master_merge_history = MergeHistory (c, common_version, "master")

  for rh in remote_history:   # remote history
    if rh[0] > common_version:
      diff = rh[1]
      changes = parse_diff (load_diff (diff))
      master_merge_history.add_changes (rh[0], changes)
      total_patch_count += 1

  # ANALYZE local history
  local_merge_history = MergeHistory (c, common_version, "local")

  for lh in local_history:    # local history
    if lh[0] > common_version:
      diff = lh[1]
      changes = parse_diff (load_diff (diff))
      local_merge_history.add_changes (lh[0], changes)
      total_patch_count += 1

  # ASK USER
  inode_ignore_change = dict()
  restore_inode = dict()
  use_both_versions = dict()

  auto_resolver = AutoConflictResolver (c, repo, common_version, master_merge_history, local_merge_history)
  user_resolver = UserConflictResolver (c, repo, common_version, master_merge_history, local_merge_history)
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
      # try to handle this conflict automatically
      choice = auto_resolver.resolve (conflict)

      # ask user if this did not work
      if choice == "":
        choice = user_resolver.resolve (conflict)
    if choice == "l":
      restore_inode[conflict] = True
    elif choice == "m":
      inode_ignore_change[conflict] = True
    elif choice == "b":
      use_both_versions[conflict] = True
    elif choice == "a":
      return

  # REVERT to common version
  revert (repo, common_version, verbose = False)

  master_version = common_version

  patch_count = 1
  # APPLY master history
  for rh in remote_history:
    if rh[0] > common_version:
      diff = rh[1]
      diff_file = os.path.join ("objects", make_object_filename (diff))

      status_line.update ("patch %d/%d" % (patch_count, total_patch_count))
      patch_count += 1

      apply (repo, xzcat (diff_file), diff, verbose = False)
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
    status_line.update ("patch %d/%d" % (patch_count, total_patch_count))
    apply (repo, new_diff, verbose = False)

  # we count the "middle-patch" even if its empty to be able to predict
  # the number of patches
  patch_count += 1

  # APPLY modified local history

  for lh in local_history:
    if lh[0] > common_version:
      # adapt diff to get rid of conflicts
      changes = local_merge_history.get_changes_without (lh[0], inode_ignore_change)

      new_diff = diff_rewriter.rewrite (changes)
      # apply modified diff
      status_line.update ("patch %d/%d" % (patch_count, total_patch_count))
      patch_count += 1
      apply (repo, new_diff, verbose = False)

  diff_rewriter.show_changes()

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
    count = 1
    for diff in ff_apply:
      diff_file = os.path.join ("objects", make_object_filename (diff))
      status_line.update ("patch %d/%d - fast forward" % (count, len (ff_apply)))
      apply (repo, xzcat (diff_file), diff, server = server, verbose = False)
      count += 1
  else:
    history_merge (c, repo, local_history, remote_history, pull_args)
