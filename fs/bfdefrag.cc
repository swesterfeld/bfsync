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

static int signal_received = 0;

static void
signal_handler (int signum)
{
  signal_received = 1;
}

bool
output_needs_update()
{
  static int last_time = 0;
  int t = time (NULL);
  if (t != last_time)
    {
      last_time = t;
      return true;
    }
  return false;
}

void
dump_update_status (const string& dump_filename, int n_records)
{
  printf ("\rDumping database to %s: %d records ... ", dump_filename.c_str(), n_records);
  fflush (stdout);
}

int
dump_db (BDB *bdb, const string& dump_filename)
{
  dump_update_status (dump_filename, 0);

  FILE *dump_file = fopen (dump_filename.c_str(), "w");

  DbcPtr dbc (bdb);

  Dbt key;
  Dbt data;
  DataOutBuffer out;
  GChecksum *sum = g_checksum_new (G_CHECKSUM_SHA1);

  int n_records = 0;

  int ret = dbc->get (&key, &data, DB_FIRST);
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

      n_records++;

      if (output_needs_update())
        dump_update_status (dump_filename, n_records);

      if (signal_received)
        {
          printf ("\nTerminated during dump.\n");
          return 1;
        }
      ret = dbc->get (&key, &data, DB_NEXT);
    }
  dump_update_status (dump_filename, n_records);

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

  printf ("done.\n");
  return 0;
}

void
verify_update_status (int n_records)
{
  printf ("\rVerifying dump: %d records ... ", n_records);
  fflush (stdout);
}

int
verify_dump (const string& dump_filename)
{
  verify_update_status (0);

  FILE *file = fopen (dump_filename.c_str(), "r");

  GChecksum *sum = g_checksum_new (G_CHECKSUM_SHA1);

  int n_records = 0;

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

      n_records++;

      if (output_needs_update())
        verify_update_status (n_records);

      guchar data[klen + dlen];
      fread (&data, klen + dlen, 1, file);
      g_checksum_update (sum, data, klen + dlen);

      if (signal_received)
        {
          printf ("\nTerminated during verify.\n");
          return 1;
        }
    }

  verify_update_status (n_records);

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

void
restore_update_status (const string& dump_filename, int n_records)
{
  printf ("\rRestoring dump %s: %d records ... ", dump_filename.c_str(), n_records);
  fflush (stdout);
}

int
restore_db (BDB *bdb, const string& dump_filename)
{
  restore_update_status (dump_filename, 0);

  FILE *file = fopen (dump_filename.c_str(), "r");
  Db *db = bdb->get_db();

  int n_records = 0;
  int OPS = 0;

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

      n_records++;
      OPS++;

      if (OPS >= 20000)
        {
          /* keep number of locks limited by splitting transactions every once in a while */
          bdb->commit_transaction();
          bdb->begin_transaction();
          OPS = 0;
        }

      if (output_needs_update())
        restore_update_status (dump_filename, n_records);

      if (signal_received)
        {
          printf ("\nTerminated during restore.\n");
          return 1;
        }
    }
  fclose (file);
  bdb->commit_transaction();

  restore_update_status (dump_filename, n_records);

  unlink (dump_filename.c_str());

  printf ("done.\n");
  return 0;
}

int
main (int argc, char **argv)
{
  bool continue_defrag = false;

  int opt;
  while ((opt = getopt (argc, argv, "c")) != -1)
    {
      switch (opt)
        {
          case 'c': continue_defrag = true;
                    break;
          default:  assert (false);
        }
    }
  if (argc - optind != 1)
    {
      printf ("usage: bfdefrag [ -c ] <db>\n");
      exit (1);
    }

  /* handle interruptions */
  signal (SIGINT, signal_handler);

  string bdb_name = argv[optind];

  BDB *bdb = bdb_open (bdb_name, 16, false);
  if (!bdb)
    {
      printf ("error opening db %s\n", bdb_name.c_str());
      exit (1);
    }

  string dump_filename = string_printf ("%s/bdb/defrag.dump", bdb_name.c_str());

  int rc = 0;

  if (!continue_defrag)
    {
      if (rc == 0)
        {
          FILE *test_dump = fopen (dump_filename.c_str(), "r");
          if (test_dump)
            {
              printf ("dump file %s already exists, exiting.\n", dump_filename.c_str());
              fclose (test_dump);
              rc = 1;
            }
        }
      if (rc == 0)
        {
          rc = dump_db (bdb, dump_filename);
        }
    }

  if (rc == 0)
    {
      rc = verify_dump (dump_filename);
    }

  if (rc == 0)
    {
      // get rid of old data
      bdb->close (BDB::CLOSE_TRUNCATE);
      delete bdb;

      bdb = bdb_open (bdb_name, 16, false);
      if (!bdb)
        {
          printf ("error opening db %s\n", bdb_name.c_str());
          exit (1);
        }
    }

  if (rc == 0)
    {
      rc = restore_db (bdb, dump_filename);
    }

  if (!bdb->close())
    {
      printf ("error closing db %s\n", bdb_name.c_str());
      exit (1);
    }

  return rc;
}
