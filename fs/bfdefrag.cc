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

#include <stdio.h>
#include <stdlib.h>

#include "bfbdb.hh"

using namespace BFSync;
using std::string;

int
main (int argc, char **argv)
{
  if (argc != 2)
    {
      printf ("usage: bfdefrag <db>\n");
      exit (1);
    }

  BDB *bdb = bdb_open (argv[1], 16, false);
  if (!bdb)
    {
      printf ("error opening db %s\n", argv[1]);
      exit (1);
    }

  string dump_filename = string_printf ("%s/bdb/defrag.dump", argv[1]);
  FILE *dump_file = fopen (dump_filename.c_str(), "w");

  Db *db = bdb->get_db();
  Dbc *dbcp;

  /* Acquire a cursor for the database. */
  int ret;
  if ((ret = db->cursor (NULL, &dbcp, 0)) != 0)
    {
      db->err (ret, "DB->cursor");
      return 1;
    }

  Dbt key;
  Dbt data;

  ret = dbcp->get (&key, &data, DB_FIRST);
  while (ret == 0)
    {
      fwrite (key.get_data(), key.get_size(), 1, dump_file);
      fputc (0, dump_file);
      fwrite (data.get_data(), data.get_size(), 1, dump_file);
      fputc (0, dump_file);

      ret = dbcp->get (&key, &data, DB_NEXT);
    }

  if (!bdb->close())
    {
      printf ("error closing db %s\n", argv[1]);
      exit (1);
    }
}
