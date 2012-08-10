/*
  bfsync: Big File synchronization tool

  Copyright (C) 2011-2012 Stefan Westerfeld

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "bfsyncdb.hh"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

using std::string;
using std::vector;

#define VERSION 1000000 // hopefully so far in the future that its the _last_ version

void
walk (BDBPtr& bdb_ptr, const ID& id, const string& prefix)
{
  INode inode = bdb_ptr.load_inode (id_root(), VERSION);
  if (inode.valid)
    {
      if (prefix.size())
        {
          printf ("%s\n", prefix.c_str());
        }
      else
        {
          printf ("/\n");
        }
      if (inode.type == BFSync::FILE_DIR)
        {
          vector<Link> links = bdb_ptr.load_links (id, VERSION);
          for (vector<Link>::const_iterator li = links.begin(); li != links.end(); li++)
            {
              const string& inode_name = prefix + "/" + li->name;
              walk (bdb_ptr, li->inode_id, inode_name);
            }
        }
    }
}

void
inr_walk (INodeRepo& inr, INodeRepoINode& inode, const string& prefix)
{
  static int OPS = 0;

  if (inode.valid())
    {
      OPS++;
      if (OPS > 100000)
        {
          inr.delete_unused_keep_count (100000);
          OPS = 0;
        }

      if (prefix.size())
        {
          printf ("%s\n", prefix.c_str());
        }
      else
        {
          printf ("/\n");
        }
      if (inode.type() == BFSync::FILE_DIR)
        {
          vector<string> children = inode.get_child_names (VERSION);

          for (vector<string>::const_iterator ci = children.begin(); ci != children.end(); ci++)
            {
              const string& inode_name = prefix + "/" + *ci;

              INodeRepoINode child_inode = inode.get_child (VERSION, *ci);
              inr_walk (inr, child_inode, inode_name);
            }
        }
    }
}

int
main (int argc, char **argv)
{
  int opt;
  bool use_inode_repo = false;
  while ((opt = getopt (argc, argv, "i")) != -1)
    {
      switch (opt)
        {
          case 'i': use_inode_repo = true;
                    break;
          default:  assert (false);
        }
    }
  if (argc - optind != 1)
    {
      printf ("usage: bfdbdump [ -x ] <db>\n");
      exit (1);
    }

  BDBPtr bdb_ptr = open_db (argv[optind], 500, false);
  if (!bdb_ptr.open_ok())
    {
      printf ("error opening db %s\n", argv[optind]);
      exit (1);
    }

  printf ("# use_inode_repo=%s\n", use_inode_repo ? "true" : "false");
  if (use_inode_repo)
    {
      INodeRepo inr (bdb_ptr);
      INodeRepoINode inode (inr.load_inode (id_root(), VERSION));

      inr_walk (inr, inode, "");
    }
  else
    {
      walk (bdb_ptr, id_root(), "");
    }
}
