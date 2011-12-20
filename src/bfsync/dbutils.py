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

def create_tables (c):
  c.execute ('''CREATE TABLE inodes
                 (
                   vmin     integer,
                   vmax     integer,
                   id       text,
                   uid      integer,
                   gid      integer,
                   mode     integer,
                   type     text,
                   hash     text,
                   link     text,
                   size     integer,
                   major    integer,
                   minor    integer,
                   nlink    integer,    /* number of hard links */
                   ctime    integer,
                   ctime_ns integer,
                   mtime    integer,
                   mtime_ns integer
                 )''')
  c.execute ('''CREATE INDEX inodes_idx ON inodes (id)''')
  c.execute ('''CREATE TABLE links
                 (
                   vmin     integer,
                   vmax     integer,
                   dir_id   text,
                   inode_id text,
                   name     text
                 )''')
  c.execute ('''CREATE INDEX links_idx ON links (dir_id)''')
  c.execute ('''CREATE TABLE history
                 (
                   version integer,
                   hash    text,
                   author  text,
                   message text,
                   time    integer
                 )''')
  c.execute ('''CREATE TABLE local_inodes
                 (
                   id      text,
                   ino     integer
                 )''')
  c.execute ('''CREATE INDEX local_inodes_idx_id ON local_inodes (id)''')
  c.execute ('''CREATE INDEX local_inodes_idx_ino ON local_inodes (ino)''')
  c.execute ('''CREATE TABLE temp_files
                 (
                   name    text,
                   pid     integer
                 )''')

def init_tables (c):
  c.execute ('''PRAGMA default_cache_size=%d''' % (1024 * 1024))     # use 128M cache size
  c.execute ('''INSERT INTO history VALUES (1, "", "", "", 0)''')
