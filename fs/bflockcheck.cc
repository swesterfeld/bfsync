// Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

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
  if (argc != 2)
    {
      printf ("usage: bflockcheck <db>\n");
      exit (1);
    }

  BDB *bdb = bdb_open (argv[1], 16, false);
  if (!bdb)
    {
      printf ("error opening db %s\n", argv[optind]);
      exit (1);
    }

  DbEnv *db_env = bdb->get_db_env();
  int status = 0, aborted = 0;
  while (1)
    {
      usleep (20000);

      int ret = db_env->lock_detect (0, DB_LOCK_DEFAULT, &aborted);
      g_assert (ret == 0);
      if (aborted)
        printf ("aborted %d\n", aborted);

      status = (status + 1) & 3;
      printf ("%c\r", "/-\\|"[status]);
      fflush (stdout);
    }
  if (!bdb->close())
    {
      printf ("error closing db %s\n", argv[1]);
      exit (1);
    }
}
