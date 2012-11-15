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

import subprocess
import cPickle
import os
from utils import connect_db
from remoteutils import *
from TransferList import TransferParams

LOCAL = 1
SSH = 2

class RemoteRepo:
  def __init__ (self, url, rsh):
    self.url = url
    self.rsh = rsh

    url_list = url.split (":")
    if len (url_list) == 1:
      self.path = os.path.abspath (url)
      self.conn = LOCAL
      self.repo = connect_db (self.path)
    elif len (url_list) == 2:
      self.host = url_list[0]
      self.path = url_list[1]
      self.conn = SSH
      command = [self.rsh, self.host, "bfsync", "remote", self.path]
      self.remote_p = subprocess.Popen (command, stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    else:
      raise Exception ("unknown url type %s, neither local nor remote?" % url)

    # check compatibility of remote bfsync version with local bfsync version
    local_version = bfsyncdb.repo_version()
    remote_version = self.version()
    if local_version != remote_version:
      raise BFSyncError ("incompatible version: remote bfsync version is '%s', local version is '%s'" % (
                       remote_version, local_version))

  def get_history (self):
    if self.conn == LOCAL:
      return remote_history (self.repo)
    else:
      self.remote_p.stdin.write ("history\n")
      result_len = int (self.remote_p.stdout.readline())
      return cPickle.loads (self.remote_p.stdout.read (result_len))

  def get_tags (self):
    if self.conn == LOCAL:
      return remote_tags (self.repo)
    else:
      self.remote_p.stdin.write ("tags\n")
      result_len = int (self.remote_p.stdout.readline())
      return cPickle.loads (self.remote_p.stdout.read (result_len))

  def update_history (self, delta_history):
    if self.conn == LOCAL:
      remote_update_history (self.repo, delta_history)
    else:
      self.remote_p.stdin.write ("update-history\n")
      delta_hist_str = cPickle.dumps (delta_history)
      self.remote_p.stdin.write ("%s\n%s" % (len (delta_hist_str), delta_hist_str))
      self.remote_p.stdin.flush()
      result_len = int (self.remote_p.stdout.readline())
      return self.remote_p.stdout.read (result_len)

  def need_objects (self, table):
    if self.conn == LOCAL:
      return remote_need_objects (self.repo, table)
    else:
      table_str = cPickle.dumps (table)
      self.remote_p.stdin.write ("need-objects\n%s\n%s" % (len (table_str), table_str))
      result_len = int (self.remote_p.stdout.readline())
      return cPickle.loads (self.remote_p.stdout.read (result_len))

  def ls (self, need_hashes):
    if self.conn == LOCAL:
      return remote_ls (self.repo, need_hashes)
    else:
      self.remote_p.stdin.write ("ls\n")
      need_hashes_str = cPickle.dumps (need_hashes)
      self.remote_p.stdin.write ("%s\n%s" % (len (need_hashes_str), need_hashes_str))
      self.remote_p.stdin.flush()
      result_len = int (self.remote_p.stdout.readline())
      return cPickle.loads (self.remote_p.stdout.read (result_len))

  def send_cmd (self, command, param):
    param_str = cPickle.dumps (param)
    self.remote_p.stdin.write ("%s\n%d\n%s" % (command, len (param_str), param_str))

  def get_objects (self, repo, tlist, tparams):
    if self.conn == LOCAL:
      tlist.copy_files (repo, self.repo)
    else:
      self.send_cmd ("send", tparams)
      tlist.send_list (self.remote_p.stdin)
      tlist.receive_files (repo, self.remote_p.stdout, True)

  def put_objects (self, repo, tlist, tparams):
    if self.conn == LOCAL:
      tlist.copy_files (self.repo, repo)
    else:
      self.remote_p.stdin.write ("receive\n")
      tlist.send_list (self.remote_p.stdin)
      tlist.send_files (repo, self.remote_p.stdin, True, tparams)
      self.remote_p.stdin.flush()

  def version (self):
    if self.conn == LOCAL:
      return bfsyncdb.repo_version()
    else:
      self.remote_p.stdin.write ("version\n")
      result_len = int (self.remote_p.stdout.readline())
      return cPickle.loads (self.remote_p.stdout.read (result_len))
