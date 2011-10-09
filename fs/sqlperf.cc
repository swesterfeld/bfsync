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
  SQLStatement stmt ("insert into inodes values (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);");

  for (size_t j = 0; j < 300000; j++)
    {
      stmt.reset();
      //stmt.clear_bindings();

      for (int i = 0; i < 15; i++)
        stmt.bind_int (1 + i, j * 100 + i);

      stmt.step();
    }
  printf ("success=%s\n", stmt.success() ? "true" : "false");
  return true;
}

bool
perf_exec()
{
  string sql;

  for (int i = 0; i < 300000; i++)
    {
      char *sql_c = g_strdup_printf ("insert into inodes values (1, 1, 'root', 0, 0, 123, 'dir', '', '', 0, 0, %d, 0, 0, 0);", i);
      sql += sql_c;
      g_free (sql_c);
    }

  int rc = sqlite3_exec (sqlite_db(), sql.c_str(), NULL, NULL, NULL);
  if (rc != SQLITE_OK)
    {
      printf ("sql exec NOK rc = %d\n", rc);
      return false;
    }
  return true;
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

  for (int i = 0; i < 2; i++)
    {
      double start_t = gettime();
      rc = sqlite3_exec (sqlite_db(), "begin", NULL, NULL, NULL);
      if (i == 0)
        {
          printf ("perf_repeat_stmt...\n");
          perf_repeat_stmt();
        }
      else if (i == 1)
        {
          printf ("perf_exec...\n");
          perf_exec();
        }
      rc = sqlite3_exec (sqlite_db(), "commit", NULL, NULL, NULL);
      double end_t = gettime();

      printf ("time for sql: %.2fms\n", (end_t - start_t) * 1000);
      printf ("inserts/sec:  %.f\n", 300000 / (end_t - start_t));
    }

  if (sqlite3_close (sqlite_db()) != SQLITE_OK)
    {
      printf ("sqlperf: can't close db\n");
    }
}
