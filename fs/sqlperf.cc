#include <sqlite3.h>
#include <stdio.h>
#include <sys/time.h>
#include <glib.h>

#include <string>

using std::string;

static double
gettime()
{
  timeval tv;
  gettimeofday (&tv, 0);

  return tv.tv_sec + tv.tv_usec / 1000000.0;
}

int
main()
{
  sqlite3 *db_ptr;

  int rc = sqlite3_open ("test/db", &db_ptr);
  if (rc != SQLITE_OK)
    {
      printf ("sqlperf: error opening db: %d\n", rc);
      return 1;
    }
  string sql = "begin;";
  for (int i = 0; i < 10; i++)
    {
      char *sql_c = g_strdup_printf ("insert into inodes values (1, 1, 'root', 0, 0, 123, 'dir', '', '', 0, 0, %d, 0, 0, 0);", i);
      sql += sql_c;
      g_free (sql_c);
    }
  sql += "commit;";
  printf ("sql: %s\n", sql.c_str());
  double start_t = gettime();

#if 0
  // prepare
  sqlite3_stmt *stmt_ptr;
  rc = sqlite3_prepare_v2 (db_ptr, sql.c_str(), sql.size(), &stmt_ptr, NULL);
  if (rc != SQLITE_OK)
    return false;

  rc = sqlite3_step (stmt_ptr);
#endif
  rc = sqlite3_exec (db_ptr, sql.c_str(), NULL, NULL, NULL);
  if (rc != SQLITE_OK)
    {
      printf ("sql exec NOK rc = %d\n", rc);
    }
#if 0
  sqlite3_finalize (stmt_ptr);
#endif
  double end_t = gettime();
  printf ("time for sql: %.2fms\n", (end_t - start_t) * 1000);
  if (sqlite3_close (db_ptr) != SQLITE_OK)
    {
      printf ("sqlperf: can't close db\n");
    }
}
