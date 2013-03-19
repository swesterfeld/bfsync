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
      printf ("usage: bfdbdump [ -x ] <db>\n");
      exit (1);
    }

  BDB *bdb = bdb_open (argv[1], 16, false);
  if (!bdb)
    {
      printf ("error opening db %s\n", argv[optind]);
      exit (1);
    }

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

  vector< vector<char> > all_keys;
  ret = dbcp->get (&key, &data, DB_FIRST);
  while (ret == 0)
    {
      vector<char> key_raw ((char *) key.get_data(), (char *) key.get_data() + key.get_size());
      all_keys.push_back (key_raw);

      ret = dbcp->get (&key, &data, DB_NEXT);
    }
  dbcp->close();

  bdb->begin_transaction();
  DbTxn *txn = bdb->get_transaction();
  for (size_t i = 0; i < all_keys.size(); i++)
    {
      vector<char>& key_raw = all_keys [g_random_int_range (0, all_keys.size())];

      if (key_raw.size())
        {
          Dbt xkey (&key_raw[0], key_raw.size());
          Dbt xdata;

          ret = bdb->get_db()->get (txn, &xkey, &xdata, DB_RMW);
          g_assert (ret == 0);
          key_raw.clear();
        }
    }
  bdb->abort_transaction();

  if (!bdb->close())
    {
      printf ("error closing db %s\n", argv[1]);
      exit (1);
    }
}
