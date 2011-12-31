/*
  bfsync: Big File synchronization tool

  Copyright (C) 2011 Stefan Westerfeld

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

#include "bfbdb.hh"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

using namespace BFSync;

using std::string;
using std::vector;

int
main (int argc, char **argv)
{
  assert (argc == 2);

  if (!bdb_open (argv[1]))
    {
      printf ("error opening db %s\n", argv[1]);
      exit (1);
    }

  Db *db = BDB::the()->get_db();
  Dbc *dbcp;

  /* Acquire a cursor for the database. */
  int ret;
  if ((ret = db->cursor (NULL, &dbcp, 0)) != 0)
    {
      db->err (ret, "DB->cursor");
      return 1;
    }

  vector<char *> links, inodes, id2ino, ino2id;

  Dbt key;
  Dbt data;

  ret = dbcp->get (&key, &data, DB_FIRST);
  while (ret == 0)
    {
      char xk[key.get_size() + 1];
      memcpy (xk, (char *)key.get_data(), key.get_size());
      xk[key.get_size()] = 0;

      DataBuffer dbuffer ((char *) data.get_data(), data.get_size());

      char table = xk[key.get_size() - 1];
      xk[key.get_size() - 1] = 0;

      if (table == BDB_TABLE_INODES)
        {
          int vmin = dbuffer.read_uint32();
          int vmax = dbuffer.read_uint32();
          int uid = dbuffer.read_uint32();
          int gid = dbuffer.read_uint32();
          int mode = dbuffer.read_uint32();
          int type = dbuffer.read_uint32();
          string hash = dbuffer.read_string();
          string link = dbuffer.read_string();
          int size = dbuffer.read_uint32(); // FIXME
          int major = dbuffer.read_uint32();
          int minor = dbuffer.read_uint32();
          int nlink = dbuffer.read_uint32();
          int ctime = dbuffer.read_uint32();
          int ctime_ns = dbuffer.read_uint32();
          int mtime = dbuffer.read_uint32();
          int mtime_ns = dbuffer.read_uint32();

          inodes.push_back (g_strdup_printf ("%s=%d|%d|%d|%d|%o|%d|%s|%s|%d|%d|%d|%d|%d|%d|%d|%d",
                                             xk, vmin, vmax, uid, gid, mode, type, hash.c_str(), link.c_str(),
                                             size, major, minor, nlink, ctime, ctime_ns, mtime, mtime_ns));
        }
      else if (table == BDB_TABLE_LINKS)
        {
          int vmin = dbuffer.read_uint32();
          int vmax = dbuffer.read_uint32();
          string inode_id = dbuffer.read_string();
          string name = dbuffer.read_string();

          links.push_back (g_strdup_printf ("%s=%d|%d|%s|%s", xk, vmin, vmax, inode_id.c_str(), name.c_str()));
        }
      else if (table == BDB_TABLE_LOCAL_ID2INO)
        {
          int ino = dbuffer.read_uint32();

          id2ino.push_back (g_strdup_printf ("%s=%d", xk, ino));
        }
      else if (table == BDB_TABLE_LOCAL_INO2ID)
        {
          DataBuffer kbuffer ((char *) key.get_data(), key.get_size());

          int   ino = kbuffer.read_uint32();
          string id = dbuffer.read_string();

          ino2id.push_back (g_strdup_printf ("%d=%s", ino, id.c_str()));
        }
      else
        {
          printf ("unknown record type\n");
        }

      ret = dbcp->get (&key, &data, DB_NEXT_DUP);
      if (ret != 0)
        ret = dbcp->get (&key, &data, DB_NEXT);
    }

  printf ("\n");
  printf ("INodes:\n");
  printf ("=======\n\n");

  for (size_t i = 0; i < inodes.size(); i++)
    printf ("%s\n", inodes[i]);

  printf ("\n");
  printf ("Links:\n");
  printf ("======\n\n");

  for (size_t i = 0; i < links.size(); i++)
    printf ("%s\n", links[i]);

  printf ("\n");
  printf ("ID2ino:\n");
  printf ("======\n\n");

  for (size_t i = 0; i < id2ino.size(); i++)
    printf ("%s\n", id2ino[i]);

  printf ("\n");
  printf ("ino2ID:\n");
  printf ("======\n\n");

  for (size_t i = 0; i < ino2id.size(); i++)
    printf ("%s\n", ino2id[i]);

  printf ("\n");

  if (!bdb_close())
    {
      printf ("error closing db %s\n", argv[1]);
      exit (1);
    }
}
