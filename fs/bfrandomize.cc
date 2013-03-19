// Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

#include <stdio.h>
#include <stdlib.h>

#include <algorithm>

#include "bfbdb.hh"

using namespace BFSync;

using std::vector;

struct Record
{
  vector<unsigned char> key;
  vector<unsigned char> data;
};

vector<Record> records;

void
load (BDB *bdb)
{
  DbcPtr dbc (bdb);
  Dbt key;
  Dbt data;

  unsigned char *p;

  int ret = dbc->get (&key, &data, DB_FIRST);
  while (ret == 0)
    {
      Record r;

      p = (unsigned char *) key.get_data();
      r.key.insert (r.key.end(), p, p + key.get_size());

      p = (unsigned char *) data.get_data();
      r.data.insert (r.data.end(), p, p + data.get_size());

      records.push_back (r);

      printf ("%zd\r", records.size());
      fflush (stdout);

      ret = dbc->get (&key, &data, DB_NEXT);
    }
  printf ("%zd\n", records.size());
}

void
restore (BDB *bdb)
{
  Db *db = bdb->get_db();
  int OPS = 0;

  bdb->begin_transaction();
  for (vector<Record>::iterator ri = records.begin(); ri != records.end(); ri++)
    {
      Dbt key (&ri->key[0], ri->key.size());
      Dbt data (&ri->data[0], ri->data.size());

      int ret = db->put (bdb->get_transaction(), &key, &data, 0);
      assert (ret == 0);

      OPS++;
      if (OPS >= 20000)
        {
          /* keep number of locks limited by splitting transactions every once in a while */
          bdb->commit_transaction();
          bdb->begin_transaction();
          OPS = 0;
        }
    }
  bdb->commit_transaction();
}

int
main (int argc, char **argv)
{
  if (argc != 2)
    {
      printf ("usage: bfrandomize <db>\n");
      exit (1);
    }

  BDB *bdb = bdb_open (argv[1], 16, false);
  if (!bdb)
    {
      printf ("error opening db %s\n", argv[1]);
      exit (1);
    }

  // load records into memory
  load (bdb);

  // get rid of old data
  bdb->close (BDB::CLOSE_TRUNCATE);
  delete bdb;

  bdb = bdb_open (argv[1], 16, false);
  if (!bdb)
    {
      printf ("error opening db %s\n", argv[1]);
      exit (1);
    }

  // shuffle records
  std::random_shuffle (records.begin(), records.end());

  // restore database
  restore (bdb);

  if (!bdb->close())
    {
      printf ("error closing db %s\n", argv[1]);
      exit (1);
    }
}
