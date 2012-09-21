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

#include "bfsyncdb.hh"
#include "bfsyncfs.hh"
#include "bfleakdebugger.hh"

extern "C" {
// #include <Python.h>
extern int PyErr_CheckSignals();
}

using std::string;
using std::vector;

using BFSync::gettime;
using BFSync::string_printf;
using BFSync::DataBuffer;
using BFSync::DataOutBuffer;

static void
id_load (ID& id, DataBuffer& dbuf)
{
  id.id = BFSync::ID (dbuf);
  id.valid = true;
}

static bool
read_sxd (FILE *file, SQLExportData& data)
{
  if (feof (file))
    return true;

  char len_data[sizeof (guint32)];
  if (fread (len_data, sizeof (guint32), 1, file) == 1)
    {
      DataBuffer len_buffer (len_data, sizeof (guint32));
      guint32 len = len_buffer.read_uint32();

      char data_data[len];
      if (fread (data_data, len, 1, file) == 1)
        {
          DataBuffer data_buffer (data_data, len);

          data.filename = data_buffer.read_string();
          id_load (data.id, data_buffer);
          id_load (data.parent_id, data_buffer);
          data.uid  = data_buffer.read_uint32();
          data.gid  = data_buffer.read_uint32();
          data.mode = data_buffer.read_uint32();
          data.type = data_buffer.read_uint32();
          data.hash = data_buffer.read_string();
          data.link = data_buffer.read_string();
          data.size = data_buffer.read_uint64();
          data.major = data_buffer.read_uint32();
          data.minor = data_buffer.read_uint32();
          data.nlink = data_buffer.read_uint32();
          data.ctime = data_buffer.read_uint32();
          data.ctime_ns = data_buffer.read_uint32();
          data.mtime = data_buffer.read_uint32();
          data.mtime_ns = data_buffer.read_uint32();
          return false;
        }
      else
        {
          return true;
        }
    }
  else
    {
      return true;
    }
}

static bool
same_data (const SQLExportData& d1, const SQLExportData& d2)
{
  return d1.filename == d2.filename &&
         d1.id == d2.id && d1.parent_id == d2.parent_id &&
         d1.uid == d2.uid && d1.gid == d2.gid && d1.mode == d2.mode &&
         d1.type == d2.type && d1.hash == d2.hash && d1.link == d2.link &&
         d1.size == d2.size && d1.major == d2.major && d1.minor == d2.minor &&
         d1.nlink == d2.nlink &&
         d1.ctime == d2.ctime && d1.ctime_ns == d2.ctime_ns &&
         d1.mtime == d2.mtime && d1.mtime_ns == d2.mtime_ns;
}

//##############################################################################

SQLExportIterator::SQLExportIterator (const string& old_files, const string& new_files) :
  old_f (0),
  new_f (0),
  old_files (old_files),
  new_files (new_files)
{
}

SQLExportData
SQLExportIterator::get_next()
{
  /* open files and initialize, if this wasn't done already */
  if (!old_f)
    {
      old_f = fopen (old_files.c_str(), "r");
      new_f = fopen (new_files.c_str(), "r");

      old_eof = false;
      new_eof = false;

      next_read = BOTH;
    }

  SQLExportData eof_data;
  while (1)
    {
      if (next_read == OLD || next_read == BOTH)
        {
          old_eof = read_sxd (old_f, old_data);
        }
      if (next_read == NEW || next_read == BOTH)
        {
          new_eof = read_sxd (new_f, new_data);
        }
      if (old_eof && new_eof)
        {
          eof_data.status = SQLExportData::NONE;
          return eof_data;
        }

      if (old_eof)
        {
          next_read = NEW;
          new_data.status = SQLExportData::ADD;
          return new_data;
        }
      else if (new_eof)
        {
          next_read = OLD;
          old_data.status = SQLExportData::DEL;
          return old_data;
        }
      else
        {
          if (old_data.filename < new_data.filename)
            {
              next_read = OLD;
              old_data.status = SQLExportData::DEL;
              return old_data;
            }
          else if (old_data.filename == new_data.filename)
            {
              next_read = BOTH;
              if (!same_data (old_data, new_data))
                {
                  old_data.status = SQLExportData::MOD;
                  return old_data;
                }
            }
          else  // new_data.filename < old_data.filename
            {
              next_read = NEW;
              new_data.status = SQLExportData::ADD;
              return new_data;
            }
        }
    }
}

SQLExportIterator::~SQLExportIterator()
{
  if (old_f)
    {
      fclose (old_f);
      old_f = 0;
    }

  if (new_f)
    {
      fclose (new_f);
      new_f = 0;
    }
}

//##############################################################################

SQLExport::SQLExport (BDBPtr bdb_ptr) :
  bdb_ptr (bdb_ptr)
{
  scan_ops = 0;
  start_time = gettime();
}

bool
cmp_link_name (Link *l1, Link *l2)
{
  return l1->name < l2->name;
}

void
SQLExport::walk (const ID& id, const ID& parent_id, const string& prefix, FILE *out_file)
{
  if (sig_interrupted)
    return;

  INode inode = bdb_ptr.load_inode (id, version);
  if (inode.valid)
    {
      string filename = prefix.size() ? prefix : "/";

      // write data
      out_buffer.clear();
      out_buffer.write_string (filename);
      inode.id.id.store (out_buffer);
      parent_id.id.store (out_buffer);
      out_buffer.write_uint32 (inode.uid);
      out_buffer.write_uint32 (inode.gid);
      out_buffer.write_uint32 (inode.mode);
      out_buffer.write_uint32 (inode.type);
      out_buffer.write_string (inode.hash);
      out_buffer.write_string (inode.link);
      out_buffer.write_uint64 (inode.size);
      out_buffer.write_uint32 (inode.major);
      out_buffer.write_uint32 (inode.minor);
      out_buffer.write_uint32 (inode.nlink);
      out_buffer.write_uint32 (inode.ctime);
      out_buffer.write_uint32 (inode.ctime_ns);
      out_buffer.write_uint32 (inode.mtime);
      out_buffer.write_uint32 (inode.mtime_ns);

      // write prefix len
      len_buffer.clear();
      len_buffer.write_uint32 (out_buffer.size());

      fwrite (len_buffer.begin(), len_buffer.size(), 1, out_file);
      fwrite (out_buffer.begin(), out_buffer.size(), 1, out_file);

      maybe_split_transaction();
      scan_ops++;
      update_status();

      if (inode.type == BFSync::FILE_DIR)
        {
          vector<Link> links = bdb_ptr.load_links (id, version);

          // sort links before recursion
          vector<Link*> sorted_links (links.size());
          for (size_t l = 0; l < links.size(); l++)
            sorted_links[l] = &links[l];
          std::sort (sorted_links.begin(), sorted_links.end(), cmp_link_name);

          for (vector<Link*>::const_iterator li = sorted_links.begin(); li != sorted_links.end(); li++)
            {
              const Link* link = *li;
              const string& inode_name = prefix + "/" + link->name;

              walk (link->inode_id, id, inode_name, out_file);
            }
        }
    }
}

void
SQLExport::maybe_split_transaction()
{
  transaction_ops++;
  if (transaction_ops >= 20000)
    {
      transaction_ops = 0;

      bdb_ptr.commit_transaction();
      bdb_ptr.begin_transaction();
    }
}

void
SQLExport::update_status()
{
  const double time = gettime();
  if (fabs (time - last_status_time) > 1)
    {
      last_status_time = time;
      printf ("%d | %.2f scan_ops/sec\n", scan_ops, scan_ops / (time - start_time));
      fflush (stdout);

      if (!sig_interrupted && PyErr_CheckSignals())
        sig_interrupted = true;
    }
}

string
SQLExport::build_filelist (unsigned int version)
{
  if (filelist_map[version] != "")
    return filelist_map[version];

  string filelist_name = string_printf ("/tmp/bdb2sql.%d", version);
  filelist_map[version] = filelist_name;

  this->version = version;
  transaction_ops = 0;
  last_status_time = 0;

  FILE *file = fopen (filelist_name.c_str(), "w");
  assert (file);

  const double version_start_time = gettime();
  bdb_ptr.begin_transaction();
  walk (id_root(), id_root(), "", file);
  bdb_ptr.commit_transaction();
  const double version_end_time = gettime();
  fclose (file);

  printf ("### filelist time: %.2f\n", (version_end_time - version_start_time));
  fflush (stdout);

  return filelist_name;
}

SQLExportIterator
SQLExport::export_version (unsigned int version)
{
  sig_interrupted = false;
  string old_files = build_filelist (version - 1);
  if (sig_interrupted)
    throw BDBException (BFSync::BDB_ERROR_INTR);

  string new_files = build_filelist (version);
  if (sig_interrupted)
    throw BDBException (BFSync::BDB_ERROR_INTR);

  return SQLExportIterator (old_files, new_files);
}

//---------------------------- SQLExportData -----------------------------

static BFSync::LeakDebugger sql_export_data_leak_debugger ("(Python)BFSync::SQLExportData");

SQLExportData::SQLExportData() :
  status (NONE),
  vmin (0),
  vmax (0),
  uid (0),
  gid (0),
  mode (0),
  type (0),
  size (0),
  major (0),
  minor (0),
  nlink (0),
  ctime (0),
  ctime_ns (0),
  mtime (0),
  mtime_ns (0)
{
  sql_export_data_leak_debugger.add (this);
}

SQLExportData::SQLExportData (const SQLExportData& data) :
  status (data.status),
  filename (data.filename),
  vmin (data.vmin),
  vmax (data.vmax),
  id (data.id),
  parent_id (data.parent_id),
  uid (data.uid),
  gid (data.gid),
  mode (data.mode),
  type (data.type),
  hash (data.hash),
  link (data.link),
  size (data.size),
  major (data.major),
  minor (data.minor),
  nlink (data.nlink),
  ctime (data.ctime),
  ctime_ns (data.ctime_ns),
  mtime (data.mtime),
  mtime_ns (data.mtime_ns)
{
  sql_export_data_leak_debugger.add (this);
}

SQLExportData::~SQLExportData()
{
  sql_export_data_leak_debugger.del (this);
}
