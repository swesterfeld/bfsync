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

from RemoteRepo import RemoteRepo
from TransferList import TransferList, TransferFile, TransferParams
import os
import sys
from utils import *
from applyutils import apply
from commitutils import revert
from xzutils import xzcat
from StatusLine import status_line
from HashCache import hash_cache
from journal import run_commands, queue_command, mk_journal_entry, CMD_AGAIN, CMD_DONE
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

def get (repo, urls):
  repo_path = repo.path

  if len (urls) == 0:
    default_get = repo.config.get ("default/get")
    if len (default_get) == 0:
      raise Exception ("get: no repository specified and default/get config value empty")
    url = default_get[0]
  else:
    url = urls[0]

  remote_repo = RemoteRepo (url)

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

def put (repo, urls):
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

def push (repo, urls):
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

  def show_changes (self):
    if len (self.changes) > 0:
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
  if dir_id == "/" + "0" * 40:
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

  if inode_type == "file":
    pp += [ ("Content", inode[INODE_CONTENT]) ]
    pp += [ ("Size", inode[7]) ]

  pp += [ ("User", inode[1]) ]
  pp += [ ("Group", inode[2]) ]
  pp += [ ("Mode", "%o" % (inode[3] & 07777)) ]

  if inode_type == "symlink":
    pp += [ ("Symlink", inode[6]) ]

  if inode_type == "blockdev" or inode_type == "chardev":
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
  def __init__ (self, repo, common_version, master_merge_history, local_merge_history):
    self.repo = repo
    self.common_version = common_version
    self.master_merge_history = master_merge_history
    self.local_merge_history = local_merge_history

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
        raise Exception ("get: no repository specified and default/get config value empty")
      url = default_get[0]

      remote_repo = RemoteRepo (url)
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
      os.system (os.environ['SHELL'])
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

def history_merge (repo, local_history, remote_history, pull_args):
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
  user_resolver = UserConflictResolver (repo, common_version, master_merge_history, local_merge_history)
  conflict_ids = find_conflicts (master_merge_history, local_merge_history)
  for conflict_id in conflict_ids:
    if pull_args.always_local:
      choice = "l"
    elif pull_args.always_master:
      choice = "m"
    elif pull_args.always_both:
      if conflict_id == "0"*40:
        choice = "m"
      else:
        choice = "b"
    else:
      conflict = Conflict()
      conflict.id = conflict_id
      print "CONFLICT id=%s" % conflict.id

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

  # REVERT to common version
  revert (repo, common_version, verbose = False)
  # command based revert
  run_commands (repo)

  master_version = common_version

  patch_count = 1
  # APPLY master history
  for rh in remote_history:
    if rh[0] > common_version:
      diff = rh[1]
      file_number = repo.bdb.load_hash2file (diff)
      if file_number != 0:
        diff_file = repo.make_number_filename (file_number)
      else:
        raise Exception ("Diff for hash %s not found" % hash)

      status_line.update ("patch %d/%d" % (patch_count, total_patch_count))
      patch_count += 1

      commit_args = dict()
      commit_args["author"] = rh[2]
      commit_args["message"] = rh[3]
      commit_args["time"] = rh[4]

      apply (repo, xzcat (diff_file), diff, verbose = False, commit_args = commit_args)
      run_commands (repo)
      master_version = rh[0]

  # APPLY extra commit to be able to apply local history without problems
  diff_rewriter = DiffRewriter (repo)

  changes = []
  for inode in restore_inode:
    common_inode = db_inode (repo, common_version, inode)
    if db_inode (repo, master_version, inode):
      # inode still exists: just undo changes
      changes += [ map (str, [ "i!" ] + common_inode) ]
    else:
      # inode is gone: recreate it to allow applying local changes
      changes += [ map (str, [ "i+" ] + common_inode) ]
    changes += restore_inode_links (db_links (repo, common_version, inode), db_links (repo, master_version, inode))

  for inode in use_both_versions:
    # duplicate inode
    common_inode = db_inode (repo, common_version, inode)
    new_id = gen_id (common_inode[0])
    changes += [ map (str, [ "i+", new_id ] + common_inode[1:]) ]
    diff_rewriter.subst_inode (common_inode[0], new_id)

    # duplicate inode links
    for link in db_links (repo, common_version, inode):
      changes.append ([ "l+", link[0], link[1], new_id ])

  new_diff = diff_rewriter.rewrite (changes)

  if new_diff != "":
    status_line.update ("patch %d/%d" % (patch_count, total_patch_count))
    commit_args["author"] = "no author"
    commit_args["message"] = "automatically generated merge commit"
    apply (repo, new_diff, verbose = False, commit_args = commit_args)
    run_commands (repo)

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
      if new_diff != "":
        commit_args = dict()
        commit_args["author"] = lh[2]
        commit_args["message"] = lh[3]
        commit_args["time"] = lh[4]

        apply (repo, new_diff, verbose = False, commit_args = commit_args)
        run_commands (repo)

  status_line.cleanup()
  diff_rewriter.show_changes()

def check_uncommitted_changes (repo):
  return False

  # FIXME
  # conn = repo.conn
  # c = conn.cursor()

  # VERSION = 1
  # c.execute ('''SELECT version FROM history''')
  # for row in c:
  #   VERSION = max (row[0], VERSION)
  # c.execute ('''SELECT COUNT (*) FROM inodes WHERE vmin=%d AND vmax=%d''' % (VERSION, VERSION))
  # for row in c:
  #   if row[0] > 0:
  #     return True

  c.execute ('''SELECT COUNT (*) FROM links WHERE vmin=%d AND vmax=%d''' % (VERSION, VERSION))
  for row in c:
    if row[0] > 0:
      return True

  return False

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
    apply (self.repo, xzcat (diff_file), diff, server = self.state.server, verbose = False, commit_args = commit_args)

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

def pull (repo, args, server = True):
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
  if check_uncommitted_changes (repo):
    raise BFSyncError ("pull failed, there are uncommitted changes in this repo (commit or revert to fix this)")

  if pull_args.repo is None:
    default_pull = repo.config.get ("default/pull")
    if len (default_pull) == 0:
      raise Exception ("pull: no repository specified and default/push config value empty")
    url = default_pull[0]
  else:
    url = pull_args.repo

  remote_repo = RemoteRepo (url)
  remote_history = remote_repo.get_history()

  local_history = []

  # c.execute ('''SELECT * FROM history WHERE hash != '' ORDER BY version''')
  # local_history = []
  # for row in c:
  #   local_history += [ row ]

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
    history_merge (repo, local_history, remote_history, pull_args)

def collect (repo, args, old_cwd):
  conn = repo.conn
  c = conn.cursor()

  status_line.set_op ("COLLECT")

  # create list of required objects
  need_hash = dict()
  c.execute ('''SELECT DISTINCT hash FROM inodes''')
  for row in c:
    hash = row[0]
    if len (hash) == 40:
      dest_file = os.path.join ("objects", make_object_filename (hash))
      if not validate_object (dest_file, hash):
        need_hash[hash] = True
    status_line.update ("preparing object list... need %d files" % len (need_hash))

  dest_path = repo.make_temp_name()
  # walk dirs to find objects
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
            move_file_to_objects (repo, dest_path)
            found += 1
            found_size += size

          fcount += 1
          fsize += size
          status_line.update ("%d local files (%s) / found %d/%d files (%s)" % (fcount, format_size1 (fsize), found, len (need_hash), format_size1 (found_size)))
        except IOError:
          pass  # usually: insufficient permissions to read file
        except OSError:
          pass  # usually: file not found
