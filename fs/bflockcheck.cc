/*
  bfsync: Big File synchronization tool

  Copyright (C) 2012 Stefan Westerfeld

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
