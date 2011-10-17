#include <sqlite3.h>
#include <stdio.h>
#include <sys/time.h>
#include <glib.h>

#include <string>

#include "bfsql.hh"

using std::string;
using namespace BFSync;

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

      for (int i = 0; i < 17; i++)
        stmt.bind_int (1 + i, j * 100 + i);

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
      char *sql_c = g_strdup_printf ("insert into inodes values (1, 1, 'root', 0, 0, 123, 'dir', '', '', 0, 0, 1, %d, 0, 0, 0, 0);", i);
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

int
main()
{
  int rc = sqlite_open ("test/db");
  if (rc != SQLITE_OK)
    {
      printf ("sqlperf: error opening db: %d\n", rc);
      return 1;
    }

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

  if (sqlite3_close (sqlite_db()) != SQLITE_OK)
    {
      printf ("sqlperf: can't close db\n");
    }
}
