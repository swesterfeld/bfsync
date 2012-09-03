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
using std::vector;

int
dump_db (BDB *bdb, const string& dump_filename)
{
  printf ("Dumping database to %s... ", dump_filename.c_str());
  fflush (stdout);

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
  DataOutBuffer out;
  GChecksum *sum = g_checksum_new (G_CHECKSUM_SHA1);

  ret = dbcp->get (&key, &data, DB_FIRST);
  while (ret == 0)
    {
      out.clear();
      out.write_uint32 (key.get_size());
      out.write_uint32 (data.get_size());

      fwrite (out.begin(), out.size(), 1, dump_file);
      g_checksum_update (sum, (guchar *) out.begin(), out.size());

      fwrite (key.get_data(), key.get_size(), 1, dump_file);
      g_checksum_update (sum, (guchar *) key.get_data(), key.get_size());

      fwrite (data.get_data(), data.get_size(), 1, dump_file);
      g_checksum_update (sum, (guchar *) data.get_data(), data.get_size());

      ret = dbcp->get (&key, &data, DB_NEXT);
    }

  out.clear();
  out.write_uint32 (0);
  out.write_uint32 (0);

  fwrite (out.begin(), out.size(), 1, dump_file);
  g_checksum_update (sum, (guchar *) out.begin(), out.size());

  vector<guint8> sha1 (g_checksum_type_get_length (G_CHECKSUM_SHA1));
  gsize digest_len = sha1.size();
  g_checksum_get_digest (sum, &sha1[0], &digest_len);

  fwrite (&sha1[0], sha1.size(), 1, dump_file);
  fclose (dump_file);

  dbcp->close();
  printf ("done.\n");
  return 0;
}

int
verify_dump (const string& dump_filename)
{
  printf ("Verifying dump ... ");
  fflush (stdout);

  FILE *file = fopen (dump_filename.c_str(), "r");

  GChecksum *sum = g_checksum_new (G_CHECKSUM_SHA1);

  const int HEADER_SIZE = 8;
  char header[HEADER_SIZE];
  while (fread (header, HEADER_SIZE, 1, file) == 1)
    {
      g_checksum_update (sum, (guchar *) header, HEADER_SIZE);

      DataBuffer hbuf (header, HEADER_SIZE);
      size_t klen = hbuf.read_uint32();
      size_t dlen = hbuf.read_uint32();

      //printf ("%zd %zd\n", klen, dlen);
      if (klen == 0 && dlen == 0)
        {
          break;
        }

      guchar data[klen + dlen];
      fread (&data, klen + dlen, 1, file);
      g_checksum_update (sum, data, klen + dlen);
    }

  vector<guint8> dump_sha1 (g_checksum_type_get_length (G_CHECKSUM_SHA1));
  vector<guint8> sha1 (g_checksum_type_get_length (G_CHECKSUM_SHA1));

  gsize digest_len = dump_sha1.size();
  g_checksum_get_digest (sum, &sha1[0], &digest_len);

  fread (&dump_sha1[0], dump_sha1.size(), 1, file);
  fclose (file);

  assert (sha1.size() == dump_sha1.size());

  if (sha1 == dump_sha1)
    {
      printf ("ok.\n");
      return 0;
    }
  else
    {
      printf ("failed.\n");
      return 1;
    }
}

int
restore_db (BDB *bdb, const string& dump_filename)
{
  printf ("Restoring dump %s ... ", dump_filename.c_str());
  fflush (stdout);


  FILE *file = fopen (dump_filename.c_str(), "r");
  Db *db = bdb->get_db();

  // get rid of old contents
  db->truncate (NULL, NULL, 0);

  // return unused pages to file system
  db->compact (NULL, NULL, NULL, NULL, DB_FREE_SPACE, NULL);

  // db->compact (NULL, NULL, 0);

  bdb->begin_transaction();

  const int HEADER_SIZE = 8;
  char header[HEADER_SIZE];
  while (fread (header, HEADER_SIZE, 1, file) == 1)
    {
      DataBuffer hbuf (header, HEADER_SIZE);
      size_t klen = hbuf.read_uint32();
      size_t dlen = hbuf.read_uint32();

      //printf ("%zd %zd\n", klen, dlen);
      if (klen == 0 && dlen == 0)
        {
          break;
        }

      guchar dump_data[klen + dlen];
      fread (&dump_data, klen + dlen, 1, file);

      Dbt key (dump_data, klen);
      Dbt data (dump_data + klen, dlen);

      int ret = db->put (bdb->get_transaction(), &key, &data, 0);
      assert (ret == 0);
    }
  fclose (file);
  bdb->commit_transaction();

  printf ("done.\n");
  return 0;
}

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

  int rc;

  rc = dump_db (bdb, dump_filename);
  if (rc != 0)
    {
      exit (1);
    }

  rc = verify_dump (dump_filename);
  if (rc != 0)
    {
      exit (1);
    }

  rc = restore_db (bdb, dump_filename);
  if (rc != 0)
    {
      exit (1);
    }

  if (!bdb->close())
    {
      printf ("error closing db %s\n", argv[1]);
      exit (1);
    }
}
