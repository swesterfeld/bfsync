from RemoteRepo import RemoteRepo
from TransferList import TransferList, TransferFile
import os
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

def db_contains_link (c, VERSION, dir_id, name):
  c.execute ("SELECT * FROM links WHERE dir_id = ? AND name = ? AND ? >= vmin AND ? <= vmax",
             (dir_id, name, VERSION, VERSION))
  for row in c:
    return True
  return False

def db_link_inode (c, VERSION, dir_id, name):
  c.execute ("SELECT inode_id FROM links WHERE dir_id = ? AND name = ? AND ? >= vmin AND ? <= vmax",
             (dir_id, name, VERSION, VERSION))
  for row in c:
    return row[0]
  raise Exception ("link target for %s/%s not found" % (dir_id, name))

def db_inode (c, VERSION, id):
  c.execute ("SELECT * FROM inodes WHERE id = ? AND ? >= vmin AND ? <= vmax",
             (id, VERSION, VERSION))
  for row in c:
    return row[2:]
  return False

def db_links (c, VERSION, id):
  c.execute ("SELECT dir_id, name, inode_id FROM links WHERE inode_id = ? AND ? >= vmin AND ? <= vmax",
             (id, VERSION, VERSION))
  results = []
  for row in c:
    results += [ list (row) ]
  return results

def db_inode_nlink (c, VERSION, id):
  c.execute ("SELECT nlink FROM inodes WHERE id = ? AND ? >= vmin AND ? <= vmax",
             (id, VERSION, VERSION))
  for row in c:
    return row[0]
  return 0

def ask_user (repo, master, local, c_fmt, m_fmt, l_fmt, filename):
  while True:
    print "======================================================================"
    for i in range (len (c_fmt)):
      print "Common   ", c_fmt[i][0], ":", c_fmt[i][1]
      if c_fmt[i] != m_fmt[i]:
        print " * Master", m_fmt[i][0], ":", m_fmt[i][1]
      if c_fmt[i] != l_fmt[i]:
        print " * Local ", l_fmt[i][0], ":", l_fmt[i][1]
    print "======================================================================"
    print "The following file was modified locally and in the master history:"
    print " => '%s'" % filename
    print "======================================================================"
    print "  l  use local version"
    print "  m  use master version"
    print "  b  keep both versions"
    print "     => master version will be stored as '%s'" % filename
    print "     => local version will be stored as '%s~1'" % filename
    print "  s  start a shell to investigate"
    print "  a  abort pull completely (history will be restored to the original"
    print "     state, before the merge)"
    print "======================================================================"
    line = raw_input ("How should this conflict be resolved? ")
    if line == "l" or line == "m" or line == "b" or line == "a":
      return line
    if line == "s":
      old_cwd = os.getcwd()

      master_file = os.path.join (old_cwd, "objects", make_object_filename (master))
      local_file = os.path.join (old_cwd, "objects", make_object_filename (local))

      default_get = repo.config.get ("default/get")

      if len (default_get) == 0:
        raise Exception ("get: no repository specified and default/get config value empty")
      url = default_get[0]

      remote_repo = RemoteRepo (url)
      get_remote_objects (remote_repo, [ master ])

      os.mkdir ("merge")
      os.chdir ("merge")
      shutil.copyfile (master_file, "master")
      shutil.copyfile (local_file, "local")
      os.system (os.environ['SHELL'])
      try:
        os.remove ("master")
        os.remove ("local")
      except:
        pass
      os.chdir (old_cwd)
      os.rmdir ("merge")
    else:
      print
      print "<<< invalid choice '%s' >>>" % line
      print

def ask_user_del (repo, master_inode, local_inode, filename):
  while True:
    print "======================================================================"
    print "The following file was"
    if master_inode is None:
      print " - deleted in master history"
    else:
      print " - modified in master history"
    if local_inode is None:
      print " - deleted locally"
    else:
      print " - modified locally"
    print " => '%s'" % filename
    print "======================================================================"
    print "  l  use local version"
    print "  m  use master version"
    print "  b  keep both versions"
    print "     => master version will be stored as '%s'" % filename
    print "     => local version will be stored as '%s~1'" % filename
    print "  s  start a shell to investigate"
    print "  a  abort pull completely (history will be restored to the original"
    print "     state, before the merge)"
    print "======================================================================"
    line = raw_input ("How should this conflict be resolved? ")
    if line == "l" or line == "m" or line == "b" or line == "a":
      return line
    else:
      print
      print "<<< invalid choice '%s' >>>" % line
      print



class MergeHistory:
  def __init__ (self):
    self.master_changes = dict()
    self.local_changes = dict()

  def add_change_master (self, version, object, change):
    if not self.master_changes.has_key (object):
      self.master_changes[object]  = []
    self.master_changes[object] += [ (version, change) ]

  def add_change_local (self, version, object, change):
    if not self.local_changes.has_key (object):
      self.local_changes[object]  = []
    self.local_changes[object] += [ (version, change) ]

  def show_one (self, k):
    print "*** Changes for key %s ***" % k
    print "--- Master ---"
    if self.master_changes.has_key (k):
      for entry in self.master_changes[k]:
        print "%4d   : %s" % (entry[0], "|".join (entry[1]))
    print "--- Local ---"
    if self.local_changes.has_key (k):
      for entry in self.local_changes[k]:
        print "%4d   : %s" % (entry[0], "|".join (entry[1]))

  def show (self):
    keys = dict()
    for k in self.master_changes:
      keys[k] = True
    for k in self.local_changes:
      keys[k] = True
    for k in keys:
      self.show_one (k)

  def conflict_keys (self):
    result = []
    keys = dict()
    for k in self.master_changes:
      keys[k] = True
    for k in self.local_changes:
      if keys.has_key (k):
        result += [ k ]
    return result

def apply_inode_changes (inode, changes):
  inode = inode[:]  # copy inode
  for entry in changes:
    version = entry[0]
    change = entry[1]
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
  return datetime.datetime.fromtimestamp (sec).strftime ("%a, %d %b %Y %H:%M:%S.") + "%09d" % nsec

def pretty_format (inode):
  pp = []
  pp += [ ("id", inode[0]) ]
  pp += [ ("uid", inode[1]) ]
  pp += [ ("gid", inode[2]) ]
  pp += [ ("mode", "%o" % inode[3]) ]
  pp += [ ("type", inode[4]) ]
  pp += [ ("content", inode[5]) ]
  pp += [ ("symlink", inode[6]) ]
  pp += [ ("size", inode[7]) ]
  pp += [ ("major", inode[8]) ]
  pp += [ ("minor", inode[9]) ]
  pp += [ ("nlink", inode[10]) ]
  pp += [ ("ctime", pretty_date (inode[11], inode[12])) ]
  pp += [ ("mtime", pretty_date (inode[13], inode[14])) ]
  return pp

def auto_merge_by_ctime (master_inode, local_inode):
  print "AUTOMERGE"

  m_sec = master_inode[11]
  l_sec = local_inode[11]

  if m_sec > l_sec:
    return "m"
  if l_sec > m_sec:
    return "l"

  # same "sec" setting
  if m_sec > l_sec:
    return "m"
  if l_sec > m_sec:
    return "l"

  m_nsec = master_inode[12]
  l_nsec = local_inode[12]
  if m_nsec > l_nsec:
    return "m"
  if l_nsec > m_nsec:
    return "l"

  # same ctime
  return "m"

def update_nlink_delta (nlink_delta, inode, count):
  if not nlink_delta.has_key (inode):
    nlink_delta[inode] = 0
  nlink_delta[inode] += count

def history_merge (c, repo, local_history, remote_history, pull_args):
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
  revert (repo, common_version)
  print "apply patches:"

  # EXAMINE master/local history for merge conflicts
  merge_h = MergeHistory()

  for rh in remote_history:   # remote history
    if rh[0] > common_version:
      diff = rh[1]
      diff_file = os.path.join ("objects", make_object_filename (diff))

      changes = parse_diff (load_diff (diff))
      for change in changes:
        print "M => ", "|".join (change)
        if change[0] == "i+" or change[0] == "i!" or change[0] == "i-":
          merge_h.add_change_master (rh[0], change[1], change)

      print "applying patch %s" % diff
      os.system ("xzcat %s > tmp-diff" % diff_file)
      f = open ("tmp-diff", "r")
      apply (repo, f, diff)
      f.close()
      os.remove ("tmp-diff")

  for lh in local_history:    # local history
    if lh[0] > common_version:
      diff = lh[1]
      diff_file = os.path.join ("objects", make_object_filename (diff))

      changes = parse_diff (load_diff (diff))
      for change in changes:
        print "local => ", "|".join (change)
        if change[0] == "i+" or change[0] == "i!" or change[0] == "i-":
          merge_h.add_change_local (lh[0], change[1], change)
  inode_ignore_change = dict()
  inode_resurrect = []
  link_resurrect = []
  for ck in merge_h.conflict_keys():
    filename = printable_name (c, ck, common_version)
    print "INODE CONFLICT: %s" % filename
    #merge_h.show_one (ck)
    common_inode = list (db_inode (c, common_version, ck))
    master_inode = apply_inode_changes (common_inode, merge_h.master_changes[ck])
    local_inode  = apply_inode_changes (common_inode, merge_h.local_changes[ck])
    common_links = db_links (c, common_version, ck)
    print "COMMON LINKS: %s" % common_links
    if master_inode is None or local_inode is None:
      if pull_args.always_local:
        choice = "l"
      elif pull_args.always_master:
        choice = "m"
      else:
        choice = ask_user_del (repo, master_inode, local_inode, filename)
      if choice == "l":
        print "... local version will be used"
        if master_inode is None:
          inode_resurrect += [ common_inode ]
          link_resurrect += common_links
      elif choice == "m":
        print "... master version will be used"
        inode_ignore_change[ck] = True
    else:
      c_fmt = pretty_format (common_inode)
      m_fmt = pretty_format (master_inode)
      l_fmt = pretty_format (local_inode)
      conf_field_set = set()
      for i in range (len (c_fmt)):
        if c_fmt[i] != m_fmt[i]:
          conf_field_set.add (c_fmt[i][0])
        if c_fmt[i] != l_fmt[i]:
          conf_field_set.add (c_fmt[i][0])
      if conf_field_set.issubset (["nlink", "mtime", "ctime"]):
        choice = auto_merge_by_ctime (master_inode, local_inode)
      else:
        if pull_args.always_local:
          choice = "l"
        elif pull_args.always_master:
          choice = "m"
        else:
          choice = ask_user (repo, master_inode[5], local_inode[5], c_fmt, m_fmt, l_fmt, filename)
      if choice == "l":
        print "... local version will be used"
      elif choice == "m":
        print "... master version will be used"
        inode_ignore_change[ck] = True

  first_local_diff = True
  for lh in local_history:
    if lh[0] > common_version:

      # determine current db version
      VERSION = 1
      c.execute ('''SELECT version FROM history''')
      for row in c:
        VERSION = max (row[0], VERSION)

      # adapt diff to get rid of conflicts
      diff = lh[1]
      diff_file = os.path.join ("objects", make_object_filename (diff))

      new_diff_filename = os.path.join (repo.path, "tmp-merge-diff")
      new_diff_file = open (new_diff_filename, "w")

      nlink_delta = dict()
      changes = parse_diff (load_diff (diff))

      if first_local_diff:
        # resurrect deleted objects that should not have been deleted
        for inode in inode_resurrect:
          changes = [ map (str, [ "i+" ] + inode) ] + changes
        for link in link_resurrect:
          changes = [ map (str, [ "l+" ] + link) ] + changes
        for change in changes:
          print "::::", "|".join (change)
        first_local_diff = False

      for change in changes:
        ignore_change = False
        if change[0] == "l+":
          if db_contains_link (c, VERSION, change[1], change[2]):
            print "LINK CONFLICT"
            suffix = 1
            while db_contains_link (c, VERSION, change[1], change[2] + "~%d" % suffix):
              suffix += 1
            change[2] = change[2] + "~%d" % suffix
        if (change[0] == "i!" or change[0] == "i-" or change[0] == "i+") and inode_ignore_change.has_key (change[1]):
          ignore_change = True
        if not ignore_change:
          # gather information to fix nlink fields
          if change[0] == "l+":
            update_nlink_delta (nlink_delta, change[3], 1)
          if change[0] == "l-":
            old = db_link_inode (c, VERSION, change[1], change[2])
            update_nlink_delta (nlink_delta, old, -1)
          if change[0] == "l!":
            old = db_link_inode (c, VERSION, change[1], change[2])
            update_nlink_delta (nlink_delta, old, -1)
            update_nlink_delta (nlink_delta, change[3], 1)
          # write change to diff
          print " => ", "|".join (change)
          s = ""
          for change_field in change:
            s += change_field + "\0"
          new_diff_file.write (s)

      # fix nlink fields (which may be inaccurate due to master history / local history merge)
      for inode in nlink_delta:
        nlink = (nlink_delta[inode] + db_inode_nlink (c, VERSION, inode))
        if nlink != 0:  # deleted inodes can no longer be modified, so we assume the inode is gone now
          change = [ "i!", inode ] + [ "" ] * 14
          change[11] = "%d" % nlink

          # the resulting diff is no longer normalized (i.e. contains more than one inode change
          # per inode), but bfapply.py will re-normalize it anyway
          s = ""
          for change_field in change:
            s += change_field + "\0"
          new_diff_file.write (s)

      new_diff_file.close()

      # apply modified diff
      print "applying patch %s" % diff

      new_diff_file = open ("tmp-merge-diff")
      changes = parse_diff (new_diff_file.read())
      new_diff_file.close()
      for change in changes:
        print "L => ", "|".join (change)

      new_diff_file = open ("tmp-merge-diff")
      apply (repo, new_diff_file)
      new_diff_file.close()
      #os.system ("bfapply.py < %s" % new_diff_filename)

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
