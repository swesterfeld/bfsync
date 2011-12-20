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

#include <sqlite3.h>
#include <stdio.h>
#include <sys/time.h>
#include <glib.h>
#include <assert.h>
#include <stdlib.h>

#include <string>

#include "bfsql.hh"
#include "bfidhash.hh"

using std::string;
using namespace BFSync;

struct INode
{
  int vmin, vmax;
  ID  id;
  int uid, gid, size;
  string type;
  ID  hash;
  string link;
  int mode, major, minor;
  int nlink;
  int mtime, mtime_ns;
  int ctime, ctime_ns;
};

INode
gen_file (int i)
{
  // typical file: 1|2|bf55988b7ce86faca1acf1f91cb70ad42a336b4b|1000|1000|33188|file|c575e82047593d95df04670f20202bb9d9752b59||565|0|0|1|1321802170|502119183|1314134494|0
  INode inode;
  inode.vmin = 1;
  inode.vmax = 1;
  inode.id = ID::gen_new();
  inode.uid = 1000;
  inode.gid = 1000;
  inode.size = i;
  inode.type = "file";
  inode.hash = ID::gen_new();
  inode.link = "";
  inode.mode = 0664;
  inode.major = inode.minor = 0;
  inode.nlink = 1;
  inode.ctime = inode.mtime = 1321802170 + i;
  inode.ctime_ns = inode.mtime_ns = 502119183 + i;
  return inode;
}

void
bind_gen_inode (int j, SQLStatement& stmt)
{
  INode inode = gen_file (j);

  stmt.bind_int (1, inode.vmin);
  stmt.bind_int (2, inode.vmax);
  stmt.bind_text (3, inode.id.str());
  stmt.bind_int (4, inode.uid);
  stmt.bind_int (5, inode.gid);
  stmt.bind_int (6, inode.size);
  stmt.bind_text (7, inode.type);
  stmt.bind_text (8, inode.hash.str());
  stmt.bind_text (9, inode.link);
  stmt.bind_int (10, inode.mode);
  stmt.bind_int (11, inode.major);
  stmt.bind_int (12, inode.minor);
  stmt.bind_int (13, inode.nlink);
  stmt.bind_int (14, inode.ctime);
  stmt.bind_int (15, inode.ctime_ns);
  stmt.bind_int (16, inode.mtime);
  stmt.bind_int (17, inode.mtime_ns);
}

static double
gettime()
{
  timeval tv;
  gettimeofday (&tv, 0);

  return tv.tv_sec + tv.tv_usec / 1000000.0;
}

bool
perf_repeat_stmt()
{
  SQLStatement stmt ("insert into inodes values (?,?,?,?,?,"
                                                "?,?,?,?,?,"
                                                "?,?,?,?,?,"
                                                "?,?);");

  stmt.begin();
  for (size_t j = 0; j < 300000; j++)
    {
      stmt.reset();

      bind_gen_inode (j, stmt);

      stmt.step();
    }
  stmt.commit();

  return stmt.success();
}

bool
perf_exec()
{
  string sql = "begin;";

  for (int i = 0; i < 300000; i++)
    {
      INode inode = gen_file (i);

      char *sql_c = g_strdup_printf ("insert into inodes values (%d, %d, '%s', %d, %d, %d, '%s', '%s', '%s', %d, %d, %d, %d, %d, %d, %d, %d);",
        inode.vmin, inode.vmax, inode.id.str().c_str(), inode.uid, inode.gid, inode.size,
        inode.type.c_str(), inode.hash.str().c_str(), inode.link.c_str(),
        inode.mode, inode.major, inode.minor, inode.nlink, inode.ctime, inode.ctime_ns, inode.mtime, inode.mtime_ns);

      sql += sql_c;
      g_free (sql_c);
    }
  sql += "commit;";

  int rc = sqlite3_exec (sqlite_db(), sql.c_str(), NULL, NULL, NULL);
  if (rc != SQLITE_OK)
    {
      printf ("sql exec NOK rc = %d\n", rc);
      return false;
    }
  return true;
}

bool
perf_del()
{
  SQLStatement stmt ("DELETE FROM inodes WHERE id = ?");
  stmt.begin();
  for (size_t j = 0; j < 300000; j++)
    {
      stmt.reset();
      stmt.bind_int (1, g_random_int_range (0, 300000) * 100 + 2);
      stmt.step();
    }
  stmt.commit();

  return stmt.success();
}

void
perf_insert_many()
{
  SQLStatement d_stmt ("DELETE FROM inodes");
  d_stmt.step();
  assert (d_stmt.success());

  SQLStatement p_stmt ("PRAGMA synchronous=off");
  p_stmt.step();
  assert (p_stmt.success());

  SQLStatement stmt ("insert into inodes values (?,?,?,?,?,"
                                                "?,?,?,?,?,"
                                                "?,?,?,?,?,"
                                                "?,?);");
  for (int i = 0; i < 500; i++)
    {
      const double start_t = gettime();
      stmt.begin();
      for (size_t j = 0; j < 100000; j++)
        {
          stmt.reset();

          bind_gen_inode (j, stmt);

          stmt.step();
        }
      stmt.commit();
      const double end_t = gettime();

      printf ("inserts/sec: %d  %.f\n", i, 100000 / (end_t - start_t));
      fflush (stdout);

      assert (stmt.success());
    }
}

int
main()
{
  printf ("resetting db... ");
  fflush (stdout);
  system ("fstest.py -r");
  printf ("done.\n");

  int rc = sqlite_open ("test/repo/db");
  if (rc != SQLITE_OK)
    {
      printf ("sqlperf: error opening db: %d\n", rc);
      return 1;
    }

#if 1
  perf_insert_many();
#else
  for (int i = 0; i < 3; i++)
    {
      double start_t = gettime();
      bool   success = false;
      if (i == 0)
        {
          printf ("perf_repeat_stmt...\n");
          success = perf_repeat_stmt();
        }
      else if (i == 1)
        {
          printf ("perf_exec...\n");
          success = perf_exec();
        }
      else if (i == 2)
        {
          printf ("perf_del...\n");
          success = perf_del();
        }
      double end_t = gettime();

      printf ("success:      %s\n", success ? "true" : "false");
      printf ("time for sql: %.2fms\n", (end_t - start_t) * 1000);
      printf ("inserts/sec:  %.f\n", 300000 / (end_t - start_t));
    }
#endif

  if (sqlite3_close (sqlite_db()) != SQLITE_OK)
    {
      printf ("sqlperf: can't close db\n");
    }
}
