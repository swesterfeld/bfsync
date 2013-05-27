# bfsync: Big File synchronization tool
# Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

from RemoteRepo import RemoteRepo
from TransferList import TransferList, TransferFile, TransferParams
import os
import sys
from utils import *
from applyutils import apply
from commitutils import revert
from xzutils import xzcat, xzcat2
from StatusLine import status_line, OutputSubsampler
from HashCache import hash_cache
from journal import queue_command, mk_journal_entry, CMD_AGAIN, CMD_DONE
import argparse
import subprocess
import datetime
import random
import shutil

def get_remote_objects (repo, remote_repo, transfer_objs, tparams):
  # make a list of hashes that we need
  need_hash = dict()
  need_hash_list = []
  for thash in transfer_objs:
    if not need_hash.has_key (thash):
      if not repo.validate_object (thash):
        need_hash[thash] = True
        need_hash_list.append (thash)

  # check for objects in remote repo
  remote_list = remote_repo.ls (need_hash_list)
  tlist = TransferList()
  for rfile in remote_list:
    if need_hash.has_key (rfile.hash):
      tlist.add (TransferFile (rfile.hash, rfile.size, rfile.number))

  # do the actual copying
  remote_repo.get_objects (repo, tlist, tparams)

def get (repo, urls, rsh):
  repo_path = repo.path

  if len (urls) == 0:
    default_get = repo.config.get ("default/get")
    if len (default_get) == 0:
      raise BFSyncError ("get: no repository specified and default/get config value empty")
    url = default_get[0]
  else:
    url = urls[0]

  remote_repo = RemoteRepo (url, rsh)

  # create list of required objects
  status_line.update ("preparing transfer list...")
  objs = []
  hi = bfsyncdb.INodeHashIterator (repo.bdb)
  while True:
    hash = hi.get_next()
    if hash == "":
      break           # done
    objs.append (hash)
  del hi # free locks the iterator might hold

  # setup rate limit
  cfg_limit = repo.config.get ("get-rate-limit")
  if len (cfg_limit) == 1:
    tparams = TransferParams (int (cfg_limit[0]))
  else:
    tparams = TransferParams (0)

  get_remote_objects (repo, remote_repo, objs, tparams)

def put (repo, urls, rsh):
  repo_path = repo.path

  if len (urls) == 0:
    default_put = repo.config.get ("default/put")
    if len (default_put) == 0:
      raise BFSyncError ("put: no repository specified and default/put config value empty")
    url = default_put[0]
  else:
    url = urls[0]

  remote_repo = RemoteRepo (url, rsh)
  need_objs = remote_repo.need_objects ("inodes")

  tl = TransferList()
  for hash in need_objs:
    if repo.validate_object (hash):
      file_number = repo.bdb.load_hash2file (hash)
      if file_number != 0:
        full_name = repo.make_number_filename (file_number)
        tl.add (TransferFile (hash, os.path.getsize (full_name), file_number))

  # setup rate limit
  cfg_limit = repo.config.get ("put-rate-limit")
  if len (cfg_limit) == 1:
    tparams = TransferParams (int (cfg_limit[0]))
  else:
    tparams = TransferParams (0)

  remote_repo.put_objects (repo, tl, tparams)

def push (repo, urls, rsh):
  repo_path = repo.path

  if len (urls) == 0:
    default_push = repo.config.get ("default/push")
    if len (default_push) == 0:
      raise Exception ("push: no repository specified and default/push config value empty")
    url = default_push[0]
  else:
    url = urls[0]

  remote_repo = RemoteRepo (url, rsh)
  remote_history = remote_repo.get_history()

  local_history = []

  VERSION = 1
  while True:
    hentry = repo.bdb.load_history_entry (VERSION)
    VERSION += 1

    if not hentry.valid:
      break

    local_history += [ (hentry.version, hentry.hash, hentry.author, hentry.message, hentry.time) ]

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
    raise BFSyncError ("push failed, remote history contains commits not in local history (pull to fix this)")

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
    if repo.validate_object (hash):
      file_number = repo.bdb.load_hash2file (hash)
      if file_number == 0:
        raise Exception ("object %s has file_number == 0" % hash)
      tl.add (TransferFile (hash, os.path.getsize (repo.make_number_filename (file_number)), file_number))

  remote_repo.put_objects (repo, tl, TransferParams (0))

def load_diff (repo, hash):
  file_number = repo.bdb.load_hash2file (hash)
  if file_number != 0:
    full_name = repo.make_number_filename (file_number)
    diff = xzcat (full_name)
  else:
    raise Exception ("Diff for hash %s not found" % hash)
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

def db_link_inode (repo, VERSION, dir_id_str, name):
  dir_id = bfsyncdb.ID (dir_id_str)
  links = repo.bdb.load_links (dir_id, VERSION)
  for link in links:
    if link.name == name:
      return link.inode_id.str()
  raise Exception ("link target for %s/%s not found" % (dir_id_str, name))

  #c.execute ("SELECT inode_id FROM links WHERE dir_id = ? AND name = ? AND ? >= vmin AND ? <= vmax",
  #           (dir_id, name, VERSION, VERSION))
  #for row in c:
  #  return row[0]

class MergeHistory:
  def __init__ (self, repo, common_version, name):
    self.repo = repo
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
        return db_link_inode (self.repo, self.common_version, change[1], change[2])
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
    if self.inode_changes.has_key (inode):     # if the inode was not changed, we return an empty list
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

def db_inode (repo, VERSION, id_str):
  id = bfsyncdb.ID (id_str)
  inode = repo.bdb.load_inode (id, VERSION)
  if inode.valid:
    return [
        inode.id.str(),
        inode.uid,
        inode.gid,
        inode.mode,
        inode.type,
        inode.hash,
        inode.link,
        inode.size,
        inode.major,
        inode.minor,
        inode.nlink,
        inode.ctime,
        inode.ctime_ns,
        inode.mtime,
        inode.mtime_ns
      ]
  return False

def db_contains_link (repo, VERSION, dir_id_str, name):
  dir_id = bfsyncdb.ID (dir_id_str)
  links = repo.bdb.load_links (dir_id, VERSION)
  for link in links:
    if link.name == name:
      return True
  return False
  #c.execute ("SELECT * FROM links WHERE dir_id = ? AND name = ? AND ? >= vmin AND ? <= vmax",
  #           (dir_id, name, VERSION, VERSION))

def db_links (repo, VERSION, id_str):
  results = []
  def update_result (link):
    if link.inode_id.str() == id_str:
      results.append ([ link.dir_id.str(), link.name, link.inode_id.str() ])

  repo.foreach_inode_link (VERSION, None, update_result)
  return results
  #c.execute ("SELECT dir_id, name, inode_id FROM links WHERE inode_id = ? AND ? >= vmin AND ? <= vmax",
  #           (id, VERSION, VERSION))


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

def gen_id (template_id):
  (template_path_prefix, tid) = template_id.split ("/")
  id = template_path_prefix + "/"
  for i in range (5):
    id += "%08x" % random.randint (0, 2**32 - 1)
  return id

class DiffRewriter:
  def __init__ (self, repo):
    self.repo = repo
    self.link_rewrite = dict()
    self.subst = dict()
    self.changes = []

  def subst_inode (self, old_id, new_id):
    self.subst[old_id] = new_id

  def rewrite (self, changes):
    # determine current db version
    VERSION = self.repo.first_unused_version()

    new_diff = ""
    for change in changes:
      if change[0] == "l+":
        if self.subst.has_key (change[3]):
          change[3] = self.subst[change[3]]
        if self.subst.has_key (change[1]):
          change[1] = self.subst[change[1]]
        if db_contains_link (self.repo, VERSION, change[1], change[2]):
          filename = change[2]
          base, ext = os.path.splitext (filename)

          suffix = 1
          while True:
            # foo.gif => foo~1.gif
            newname = base + "~%d" % suffix + ext
            if not db_contains_link (self.repo, VERSION, change[1], newname):
              break
            suffix += 1

          lrkey = (change[1], change[2])
          self.link_rewrite[lrkey] = newname
          path = self.repo.printable_name (change[1], VERSION)
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

  def get_change_message (self):
    change_message = []
    if len (self.changes) > 0:
      change_message += [
        "",
        "The following local files were renamed to avoid name conflicts",
        "with the master history:",
        ""
      ]
      for old, new in self.changes:
        change_message.append (" * '%s' => '%s'" % (old, new))
      change_message.append ("")
    return change_message

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

def apply_link_changes (links, changes):
  links = links[:]  # copy links
  for change in changes:
    if change[0] == "l+":
      links += [ change[1:] ]
    if change[0] == "l-":
      new_links = []
      for l in links:
        if l[0] == change[1] and l[1] == change[2]:
          pass
        else:
          new_links += [ l ]
      links = new_links
  return links

def link_filename (repo, common_version, dir_id, merge_history):
  if dir_id == ID_ROOT:
    return "/"

  links = db_links (repo, common_version, dir_id)
  if merge_history is None:
    pass
  else:
    links = apply_link_changes (links, merge_history.get_changes (dir_id))

  for link in links:
    return os.path.join (link_filename (repo, common_version, link[0], merge_history), link[1])
  return links

def pretty_date (sec, nsec):
  return datetime.datetime.fromtimestamp (sec).strftime ("%F %H:%M:%S.") + "%d" % (nsec / 100 / 1000 / 1000)

def pretty_type (type):
  if type == bfsyncdb.FILE_REGULAR:
    return "file"
  if type == bfsyncdb.FILE_SYMLINK:
    return "symlink"
  if type == bfsyncdb.FILE_DIR:
    return "dir"
  if type == bfsyncdb.FILE_FIFO:
    return "fifo"
  if type == bfsyncdb.FILE_SOCKET:
    return "socket"
  if type == bfsyncdb.FILE_BLOCK_DEV:
    return "blockdev"
  if type == bfsyncdb.FILE_CHAR_DEV:
    return "chardev"
  raise Exception ("unknown object type %s" % type)

INODE_TYPE = 4
INODE_CONTENT = 5

INODE_CTIME = 11
INODE_CTIME_NS = 12
INODE_MTIME = 13
INODE_MTIME_NS = 14

def pretty_format (inode):
  inode_type = inode[INODE_TYPE]
  pp = []
  pp += [ ("Type", pretty_type (inode[INODE_TYPE])) ]

  if inode_type == bfsyncdb.FILE_REGULAR:
    pp += [ ("Content", inode[INODE_CONTENT]) ]
    pp += [ ("Size", inode[7]) ]

  pp += [ ("User", inode[1]) ]
  pp += [ ("Group", inode[2]) ]
  pp += [ ("Mode", "%o" % (inode[3] & 07777)) ]

  if inode_type == bfsyncdb.FILE_SYMLINK:
    pp += [ ("Symlink", inode[6]) ]

  if inode_type == bfsyncdb.FILE_BLOCK_DEV or inode_type == bfsyncdb.FILE_CHAR_DEV:
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
    mdel = ""
  else:
    mdel = "(deleted)"
  if local_inode:
    l_fmt = pretty_format (local_inode)
    ldel = ""
  else:
    ldel = "(deleted)"

  print "%-8s| %-22s| %-22s| %-22s" % ("", "Common", "Master %s" % mdel, "Local %s" % ldel)
  print "--------|-----------------------|-----------------------|----------------------"
  for i in range (len (c_fmt)):
    cdata = c_fmt[i][1]

    if master_inode:
      mdata = m_fmt[i][1]
    else:
      mdata = "!"
    if local_inode:
      ldata = l_fmt[i][1]
    else:
      ldata = "!"

    c = cdata
    l = ldata
    m = mdata

    if c_fmt[i][0] == "Content":
      c = "old content"
      if mdata != cdata:
        m = "new content 1"
      else:
        m = c
      if ldata != cdata:
        if ldata != mdata:
          l = "new content 2"
        else:
          l = m
      else:
        l = c
    if l == c:
      l = "~"
    if m == c:
      m = "~"
    if not master_inode:
      m = "!"
    if not local_inode:
      l = "!"
    print "%-8s| %-22s| %-22s| %-22s" % (c_fmt[i][0], c, m, l)

class Conflict:
  pass

class AutoConflictResolver:
  def __init__ (self, repo, common_version, master_merge_history, local_merge_history):
    self.repo = repo
    self.common_version = common_version
    self.master_merge_history = master_merge_history
    self.local_merge_history = local_merge_history

  def auto_merge_by_ctime (self, conflict):
    m_sec = conflict.master_inode[INODE_CTIME]
    l_sec = conflict.local_inode[INODE_CTIME]

    if m_sec > l_sec:
      return "m"
    if l_sec > m_sec:
      return "l"

    # m_sec == l_sec
    m_nsec = conflict.master_inode[INODE_CTIME_NS]
    l_nsec = conflict.local_inode[INODE_CTIME_NS]
    if m_nsec > l_nsec:
      return "m"
    if l_nsec > m_nsec:
      return "l"

    # same ctime
    return "m"

  def resolve (self, conflict):
    if conflict.master_inode is None or conflict.local_inode is None:
      return ""  # deletion, can't do that automatically

    if conflict.master_rename or conflict.local_rename:
      return "" # rename, can't do that automatically

    cfields = set()
    for i in range (len (conflict.common_inode)):
      cdata = conflict.common_inode[i]
      mdata = conflict.master_inode[i]
      ldata = conflict.local_inode[i]

      if cdata != mdata or cdata != ldata:
        cfields.add (i)

    if cfields.issubset ([INODE_CTIME, INODE_CTIME_NS,
                          INODE_MTIME, INODE_MTIME_NS]):
      return self.auto_merge_by_ctime (conflict)
    else:
      return ""   # could not resolve automatically

class UserConflictResolver:
  def __init__ (self, repo, common_version, master_merge_history, local_merge_history, rsh):
    self.repo = repo
    self.common_version = common_version
    self.master_merge_history = master_merge_history
    self.local_merge_history = local_merge_history
    self.rsh = rsh

  def init_merge_dir (self, filename, common_hash, master_hash, local_hash):
    merge_dir = os.path.join (self.repo.path, "merge")
    os.mkdir (merge_dir)

    # Common content
    common_file = self.repo.make_object_filename (common_hash)
    shutil.copyfile (common_file, os.path.join (merge_dir, "common_%s" % filename))

    # Master content
    if master_hash:
      default_get = self.repo.config.get ("default/get")

      if len (default_get) == 0:
        raise BFSyncError ("get: no repository specified and default/get config value empty")
      url = default_get[0]

      remote_repo = RemoteRepo (url, self.rsh)
      get_remote_objects (self.repo, remote_repo, [ master_hash ], TransferParams (0))

      master_file = self.repo.make_object_filename (master_hash)
      shutil.copyfile (master_file, os.path.join (merge_dir, "master_%s" % filename))

    # Local content
    if local_hash:
      local_file = self.repo.make_object_filename (local_hash)
      shutil.copyfile (local_file, os.path.join (merge_dir, "local_%s" % filename))

  def rm_merge_dir (self, filename):
    merge_dir = os.path.join (self.repo.path, "merge")

    try:
      os.remove (os.path.join (merge_dir, "common_%s" % filename))
      os.remove (os.path.join (merge_dir, "master_%s" % filename))
      os.remove (os.path.join (merge_dir, "local_%s" % filename))
    except:
      pass
    os.rmdir (merge_dir)

  def shell (self, filename, common_hash, master_hash, local_hash, display = None):
    old_cwd = os.getcwd()
    os.chdir ("merge")
    if display == "M":
      subprocess.call ([ "xdg-open", "master_%s" % filename ])
    elif display == "L":
      subprocess.call ([ "xdg-open", "local_%s" % filename ])
    elif display == "C":
      subprocess.call ([ "xdg-open", "common_%s" % filename ])
    else:
      subprocess.call ([ os.environ['SHELL'] ])
    os.chdir (old_cwd)

  def resolve (self, conflict):
    have_merge_dir = False

    while True:
      common_inode = conflict.common_inode
      master_inode = conflict.master_inode
      local_inode = conflict.local_inode

      common_links = conflict.common_links
      master_links = conflict.master_links
      local_links  = conflict.local_links

      common_names = []
      for link in common_links:
        common_names.append (os.path.join (link_filename (self.repo, self.common_version, link[0], None), link[1]))

      master_names = []
      for link in master_links:
        master_names.append (os.path.join (
          link_filename (self.repo, self.common_version, link[0], self.master_merge_history),
          link[1]
        ))

      local_names = []
      for link in local_links:
        local_names.append (os.path.join (
          link_filename (self.repo, self.common_version, link[0], self.local_merge_history),
          link[1]
        ))

      fullname = self.repo.printable_name (conflict.id, self.common_version)
      filename = os.path.basename (fullname)

      print "=" * 80
      print "Merge Conflict: '%s' was" % fullname
      if master_inode is None:
        print " - deleted in master history"
      else:
        print " - modified in master history"
        if conflict.master_rename:
          for n in master_names:
            print " - renamed to '%s' in master history" % n
      if local_inode is None:
        print " - deleted locally"
      else:
        print " - modified locally"
        if conflict.local_rename:
          for n in local_names:
            print " - renamed to '%s' in local history" % n
      print "=" * 80
      pretty_print_conflict (common_inode, master_inode, local_inode)
      print "=" * 80
      line = raw_input ("(m)aster / (l)ocal / (b)oth / (v)iew / (s)hell / (a)bort merge\ndisplay (M)aster content / display (L)ocal content / display (C)ommon content? ")
      if line == "v":
        print "=== MASTER ==="
        self.master_merge_history.show_changes (conflict.id)
        print "=== LOCAL ==="
        self.local_merge_history.show_changes (conflict.id)
        print "=============="
      if line == "s" or line == "C" or line == "M" or line == "L":
        conflict_type = common_inode[INODE_TYPE]
        if conflict_type != bfsyncdb.FILE_REGULAR:
          print
          print "Sorry, shell is only supported for plain files, but conflict type is %s." % pretty_type (conflict_type)
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
          if line == "s":
            display = None
          else:
            display = line

          if not have_merge_dir:
            self.init_merge_dir (filename, c_hash, m_hash, l_hash)
            have_merge_dir = True

          self.shell (filename, c_hash, m_hash, l_hash, display)
      if line == "m" or line == "l" or line == "b" or line == "a":
        if line == "l":
          print "... local version will be used"
        elif line == "m":
          print "... master version will be used"
        elif line == "b":
          print "... both versions will be used"
        elif line == "a":
          print "... aborting merge"

        # cleanup merge dir
        if have_merge_dir:
          self.rm_merge_dir (filename)

        return line

################################# MERGE ######################################

class MergeState:
  pass

class MergeCommand:
  EXEC_PHASE_REVERT_COMMON  = 1
  EXEC_PHASE_APPLY_MASTER   = 2
  EXEC_PHASE_REWRITE_DIFFS  = 3
  EXEC_PHASE_APPLY_LOCAL    = 4

  def start (self, repo, common_version, total_patch_count,
             restore_inode, use_both_versions, inode_ignore_change,
             local_history, remote_history):
    self.state = MergeState()
    self.state.common_version = common_version
    self.state.current_version = -1
    self.state.patch_count = 1
    self.state.total_patch_count = total_patch_count
    self.state.local_history = local_history
    self.state.remote_history = remote_history
    self.state.restore_inode = restore_inode
    self.state.use_both_versions = use_both_versions
    self.state.inode_ignore_change = inode_ignore_change
    self.state.exec_phase = self.EXEC_PHASE_REVERT_COMMON
    self.repo = repo

  def restart (self, repo):
    self.repo = repo

  def execute (self):
    if self.state.exec_phase == self.EXEC_PHASE_REVERT_COMMON:
      #=> spawn revert sub-command
      revert (self.repo, self.state.common_version, verbose = False)

      self.state.exec_phase += 1
      self.state.current_version = self.state.common_version

      # create new journal entry
      self.repo.bdb.begin_transaction()
      mk_journal_entry (self.repo)
      self.repo.bdb.commit_transaction()

      return CMD_AGAIN # will execute queued commands before execute() gets called again

    if self.state.exec_phase == self.EXEC_PHASE_APPLY_MASTER:
      # APPLY master history
      for rh in self.state.remote_history:
        if rh[0] > self.state.current_version:
          diff = rh[1]
          file_number = self.repo.bdb.load_hash2file (diff)
          if file_number != 0:
            diff_file = self.repo.make_number_filename (file_number)
          else:
            raise Exception ("Diff for hash %s not found" % hash)

          commit_args = {
            "author" : rh[2],
            "message" : rh[3],
            "time" : rh[4]
          }

          # unxz diff file for apply
          self.repo.bdb.begin_transaction()
          uncompressed_diff_file = self.repo.make_temp_name()
          self.repo.bdb.commit_transaction()

          xzcat2 (diff_file, uncompressed_diff_file)

          #=> spawn apply sub-command
          apply (self.repo, uncompressed_diff_file, diff, verbose = False, commit_args = commit_args)
          self.state.current_version = rh[0]

          status_line.update ("patch %d/%d" % (self.state.patch_count, self.state.total_patch_count))
          self.state.patch_count += 1

          # create new journal entry
          self.repo.bdb.begin_transaction()
          mk_journal_entry (self.repo)
          self.repo.bdb.commit_transaction()

          return CMD_AGAIN

      self.state.master_version = self.state.current_version
      self.state.exec_phase += 1

      # create new journal entry
      self.repo.bdb.begin_transaction()
      mk_journal_entry (self.repo)
      self.repo.bdb.commit_transaction()

    if self.state.exec_phase == self.EXEC_PHASE_REWRITE_DIFFS:
      # APPLY extra commit to be able to apply local history without problems
      diff_rewriter = DiffRewriter (self.repo)

      changes = []
      for inode in self.state.restore_inode:
        common_inode = db_inode (self.repo, self.state.common_version, inode)
        if db_inode (self.repo, self.state.master_version, inode):
          # inode still exists: just undo changes
          changes += [ map (str, [ "i!" ] + common_inode) ]
        else:
          # inode is gone: recreate it to allow applying local changes
          changes += [ map (str, [ "i+" ] + common_inode) ]
        changes += restore_inode_links (db_links (self.repo, self.state.common_version, inode),
                                        db_links (self.repo, self.state.master_version, inode))

      for inode in self.state.use_both_versions:
        # duplicate inode
        common_inode = db_inode (self.repo, self.state.common_version, inode)
        new_id = gen_id (common_inode[0])
        changes += [ map (str, [ "i+", new_id ] + common_inode[1:]) ]
        diff_rewriter.subst_inode (common_inode[0], new_id)

        # duplicate inode links
        for link in db_links (self.repo, self.state.common_version, inode):
          changes.append ([ "l+", link[0], link[1], new_id ])

      self.state.local_diffs = []

      def add_local_diff (diff, commit_args):
        # create temp file containing diff & append diff to local_diffs list
        self.repo.bdb.begin_transaction()
        diff_filename = self.repo.make_temp_name()
        self.state.local_diffs.append ((diff_filename, commit_args))
        self.repo.bdb.commit_transaction()

        diff_file = open (diff_filename, "w")
        diff_file.write (diff)
        diff_file.close()

      new_diff = diff_rewriter.rewrite (changes)
      commit_args = {
        "author" : "no author",
        "message" : "automatically generated merge commit"
      }
      add_local_diff (new_diff, commit_args)

      # ANALYZE local history (again) - we don't want this to be part of the state
      # because then the state would be huge
      local_merge_history = MergeHistory (self.repo, self.state.common_version, "local")

      for lh in self.state.local_history:    # local history
        if lh[0] > self.state.common_version:
          diff = lh[1]
          changes = parse_diff (load_diff (self.repo, diff))
          local_merge_history.add_changes (lh[0], changes)

      for lh in self.state.local_history:    # local history
        if lh[0] > self.state.common_version:
          diff = lh[1]
          changes = local_merge_history.get_changes_without (lh[0], self.state.inode_ignore_change)

          new_diff = diff_rewriter.rewrite (changes)
          commit_args = {
            "author"  : lh[2],
            "message" : lh[3],
            "time"    : lh[4]
          }
          add_local_diff (new_diff, commit_args)

      self.state.change_message = diff_rewriter.get_change_message()
      self.state.exec_phase += 1

      # create new journal entry
      self.repo.bdb.begin_transaction()
      mk_journal_entry (self.repo)
      self.repo.bdb.commit_transaction()

    if self.state.exec_phase == self.EXEC_PHASE_APPLY_LOCAL:
      if len (self.state.local_diffs):
        diff_file, commit_args = self.state.local_diffs[0]

        #=> spawn apply sub-command
        if os.path.getsize (diff_file) != 0:
          apply (self.repo, diff_file, verbose = False, commit_args = commit_args)
        status_line.update ("patch %d/%d" % (self.state.patch_count, self.state.total_patch_count))
        self.state.patch_count += 1

        self.state.local_diffs = self.state.local_diffs[1:]

        # create new journal entry
        self.repo.bdb.begin_transaction()
        mk_journal_entry (self.repo)
        self.repo.bdb.commit_transaction()

        return CMD_AGAIN

    status_line.cleanup()
    if len (self.state.change_message):
      print "\n".join (self.state.change_message)

    return CMD_DONE

  def get_operation (self):
    return "merge"

  def get_state (self):
    return self.state

  def set_state (self, state):
    self.state = state

def history_merge (repo, local_history, remote_history, pull_args, rsh):
  # figure out last common version
  common_version = find_common_version (local_history, remote_history)

  # initialize as one because of the middle-patch (which is neither from local nor remote history)
  total_patch_count = 1

  # ANALYZE master history
  master_merge_history = MergeHistory (repo, common_version, "master")

  for rh in remote_history:   # remote history
    if rh[0] > common_version:
      diff = rh[1]
      changes = parse_diff (load_diff (repo, diff))
      master_merge_history.add_changes (rh[0], changes)
      total_patch_count += 1

  # ANALYZE local history
  local_merge_history = MergeHistory (repo, common_version, "local")

  for lh in local_history:    # local history
    if lh[0] > common_version:
      diff = lh[1]
      changes = parse_diff (load_diff (repo, diff))
      local_merge_history.add_changes (lh[0], changes)
      total_patch_count += 1

  # ASK USER
  inode_ignore_change = dict()
  restore_inode = dict()
  use_both_versions = dict()

  auto_resolver = AutoConflictResolver (repo, common_version, master_merge_history, local_merge_history)
  user_resolver = UserConflictResolver (repo, common_version, master_merge_history, local_merge_history, rsh)
  conflict_ids = find_conflicts (master_merge_history, local_merge_history)
  for conflict_id in conflict_ids:
    if pull_args.always_local:
      choice = "l"
    elif pull_args.always_master:
      choice = "m"
    elif pull_args.always_both:
      if conflict_id == ID_ROOT:
        choice = "m"
      else:
        choice = "b"
    else:
      conflict = Conflict()
      conflict.id = conflict_id

      # determine inode content for common, local and master version
      conflict.common_inode = db_inode (repo, common_version, conflict.id)

      conflict.master_inode = apply_inode_changes (
        conflict.common_inode,
        master_merge_history.get_changes (conflict.id))

      conflict.local_inode = apply_inode_changes (
        conflict.common_inode,
        local_merge_history.get_changes (conflict.id))

      # determine inode links for common/local/master version
      conflict.common_links = db_links (repo, common_version, conflict.id)
      conflict.master_links = apply_link_changes (conflict.common_links, master_merge_history.get_changes (conflict.id))
      conflict.local_links  = apply_link_changes (conflict.common_links, local_merge_history.get_changes (conflict.id))

      conflict.common_links.sort()
      conflict.master_links.sort()
      conflict.local_links.sort()

      conflict.master_rename = (conflict.common_links != conflict.master_links)
      conflict.local_rename = (conflict.common_links != conflict.local_links)

      # try to handle this conflict automatically
      choice = auto_resolver.resolve (conflict)

      # ask user if this did not work
      if choice == "":
        choice = user_resolver.resolve (conflict)
    if choice == "l":
      restore_inode[conflict_id] = True
    elif choice == "m":
      inode_ignore_change[conflict_id] = True
    elif choice == "b":
      use_both_versions[conflict_id] = True
    elif choice == "a":
      return

  cmd = MergeCommand()
  cmd.start (repo,
    common_version = common_version,
    total_patch_count = total_patch_count,
    restore_inode = restore_inode,
    inode_ignore_change = inode_ignore_change,
    use_both_versions = use_both_versions,
    local_history = local_history,
    remote_history = remote_history)

  queue_command (cmd)
  return True


class FastForwardState:
  pass

class FastForwardCommand:
  def start (self, repo, ff_apply, server):
    self.state = FastForwardState()
    self.state.count = 0
    self.state.ff_apply = ff_apply
    self.state.server = server
    self.repo = repo

  def restart (self, repo):
    self.repo = repo

  def execute (self):
    if self.state.count == len (self.state.ff_apply):
      return CMD_DONE

    diff, commit_args = self.state.ff_apply[self.state.count]
    diff_file = self.repo.make_object_filename (diff)
    status_line.update ("patch %d/%d - fast forward" % (self.state.count + 1, len (self.state.ff_apply)))

    # unxz diff file for apply
    self.repo.bdb.begin_transaction()
    uncompressed_diff_file = self.repo.make_temp_name()
    self.repo.bdb.commit_transaction()

    xzcat2 (diff_file, uncompressed_diff_file)

    apply (self.repo, uncompressed_diff_file, diff, server = self.state.server, verbose = False, commit_args = commit_args)

    # FIXME: this could fail if
    #  - some apply commands are already spawned, but self.state.count is not updated
    self.state.count += 1

    self.repo.bdb.begin_transaction()
    mk_journal_entry (self.repo)
    self.repo.bdb.commit_transaction()

    return CMD_AGAIN # will execute queued commands before execute() gets called again

  def get_operation (self):
    return "fast-forward"

  def get_state (self):
    return self.state

  def set_state (self, state):
    self.state = state

def pull (repo, args, rsh, server = True):
  parser = argparse.ArgumentParser (prog='bfsync pull')
  parser.add_argument ('--always-local', action='store_const', const=True,
                       help='always use local version for merge conflicts')
  parser.add_argument ('--always-master', action='store_const', const=True,
                       help='always use master version for merge conflicts')
  parser.add_argument ('--always-both', action='store_const', const=True,
                       help='always use both versions for merge conflicts')
  parser.add_argument ('repo', nargs = '?')
  pull_args = parser.parse_args (args)

  repo_path = repo.path

  # Uncommitted changes?
  if repo.check_uncommitted_changes():
    raise BFSyncError ("pull failed, there are uncommitted changes in this repo (commit or revert to fix this)")

  if pull_args.repo is None:
    default_pull = repo.config.get ("default/pull")
    if len (default_pull) == 0:
      raise Exception ("pull: no repository specified and default/push config value empty")
    url = default_pull[0]
  else:
    url = pull_args.repo

  remote_repo = RemoteRepo (url, rsh)
  remote_history = remote_repo.get_history()

  local_history = []

  VERSION = 1
  while True:
    hentry = repo.bdb.load_history_entry (VERSION)
    VERSION += 1

    if not hentry.valid:
      break

    row = (hentry.version, hentry.hash, hentry.author, hentry.message, hentry.time)
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
        args = dict()
        args["author"] = rh[2]
        args["message"] = rh[3]
        args["time"] = rh[4]
        ff_apply += [ (hash, args) ]

  # Already up-to-date?
  if can_fast_forward and len (ff_apply) == 0:
    print "Already up-to-date."
    return

  # transfer required history objects
  get_remote_objects (repo, remote_repo, transfer_objs, TransferParams (0))

  if can_fast_forward:
    cmd = FastForwardCommand()

    cmd.start (repo, ff_apply, server)
    queue_command (cmd)
  else:
    history_merge (repo, local_history, remote_history, pull_args, rsh)

def collect (repo, args, old_cwd):
  status_line.set_op ("COLLECT")

  outss = OutputSubsampler()

  # create list of required objects
  need_hash = dict()
  hi = bfsyncdb.INodeHashIterator (repo.bdb)
  while True:
    hash = hi.get_next()
    if hash == "":
      break           # done
    if not repo.validate_object (hash):
      need_hash[hash] = True
      if outss.need_update():
        status_line.update ("preparing object list... need %d files" % len (need_hash))
  del hi # free locks the iterator might hold

  repo.bdb.begin_transaction()
  dest_path = repo.make_temp_name()
  repo.bdb.commit_transaction()

  def update_status():
    status_line.update ("%d local files (%s) / found %d/%d files (%s)" % (fcount, format_size1 (fsize), found, ftotal, format_size1 (found_size)))

  repo.bdb.begin_transaction()

  OPS = 0  # to keep number of operations per transaction below a pre-defined limit

  # walk dirs to find objects
  ftotal = len (need_hash)
  fcount = 0
  found = 0
  fsize = 0
  found_size = 0
  for cdir in args:
    collect_dir = os.path.abspath (os.path.join (old_cwd, cdir))
    for root, dirs, files in os.walk (collect_dir):
      for f in files:
        full_name = os.path.join (root, f)

        try:
          hash = hash_cache.compute_hash (full_name)
          size = os.path.getsize (full_name)

          if need_hash.has_key (hash):
            shutil.copyfile (full_name, dest_path)
            move_file_to_objects (repo, dest_path, need_transaction = False)
            found += 1
            found_size += size
            # copy each file content only once, after that we don't need to do it again
            del need_hash[hash]

            OPS += 1 # only move_file_to_objects actually causes BDB access

          fcount += 1
          fsize += size
          if outss.need_update():
            update_status()

          if OPS >= 20000:
            repo.bdb.commit_transaction()
            repo.bdb.begin_transaction()
            OPS = 0

        except IOError:
          pass  # usually: insufficient permissions to read file
        except OSError:
          pass  # usually: file not found
  repo.bdb.commit_transaction()
  update_status()
