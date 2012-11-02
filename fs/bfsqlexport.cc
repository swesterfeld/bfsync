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

#define __STDC_FORMAT_MACROS 1

#include "bfsyncdb.hh"
#include "bfsyncfs.hh"
#include "bfleakdebugger.hh"

#include <inttypes.h>

extern "C" {
// #include <Python.h>
extern int PyErr_CheckSignals();
}

using std::string;
using std::vector;
using std::map;

using BFSync::gettime;
using BFSync::get_basename;
using BFSync::get_dirname;
using BFSync::string_printf;
using BFSync::DataBuffer;
using BFSync::DataOutBuffer;
using BFSync::BDBError;

struct SQLExportData
{
  SQLExportData();
  SQLExportData (const SQLExportData& data);
  ~SQLExportData();

  std::string copy_from_line (const string& repo_id, unsigned int vmin) const;
  std::string delete_copy_from_line() const;

  enum { NONE, ADD, DEL, MOD } status;

  std::string   filename;
  ID            id;
  ID            parent_id;
  unsigned int  uid;
  unsigned int  gid;
  unsigned int  mode;
  unsigned int  type;
  std::string   hash;
  std::string   link;
  uint64_t      size;
  unsigned int  major;
  unsigned int  minor;
  unsigned int  nlink;
  unsigned int  ctime;
  unsigned int  ctime_ns;
  unsigned int  mtime;
  unsigned int  mtime_ns;
};

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

class SQLExportIterator
{
  string   repo_id_stripped;
  string   old_files;
  string   new_files;

  FILE    *insert_file;
  FILE    *delete_file;

  void write_insert (const SQLExportData& data, unsigned int version);
  void write_modify (const SQLExportData& data, unsigned int version);
  void write_delete (const SQLExportData& data);
public:
  SQLExportIterator (const string& repo_id_stripped, const string& old_files, const string& new_files);

  void gen_files (unsigned int version, const string& insert_filename, const string& delete_filename);
};

SQLExportIterator::SQLExportIterator (const string& repo_id_stripped, const string& old_files, const string& new_files) :
  repo_id_stripped (repo_id_stripped),
  old_files (old_files),
  new_files (new_files)
{
}

void
SQLExportIterator::write_insert (const SQLExportData& data, unsigned int version)
{
  fputs (data.copy_from_line (repo_id_stripped, version).c_str(), insert_file);
}

void
SQLExportIterator::write_delete (const SQLExportData& data)
{
  fputs (data.delete_copy_from_line().c_str(), delete_file);
}

void
SQLExportIterator::write_modify (const SQLExportData& data, unsigned int version)
{
  write_delete (data);
  write_insert (data, version);
}

void
SQLExportIterator::gen_files (unsigned int version, const string& insert_filename, const string& delete_filename)
{
  SQLExportData old_data;
  SQLExportData new_data;

  enum { OLD, NEW, BOTH } next_read = BOTH;

  FILE *old_f = fopen (old_files.c_str(), "r");
  FILE *new_f = fopen (new_files.c_str(), "r");

  bool old_eof = false;
  bool new_eof = false;

  insert_file = fopen (insert_filename.c_str(), "w");
  delete_file = fopen (delete_filename.c_str(), "w");

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
          break;
        }

      if (old_eof)
        {
          next_read = NEW;
          write_insert (new_data, version);
        }
      else if (new_eof)
        {
          next_read = OLD;
          write_delete (old_data);
        }
      else
        {
          if (old_data.filename < new_data.filename)
            {
              next_read = OLD;
              write_delete (old_data);
            }
          else if (old_data.filename == new_data.filename)
            {
              next_read = BOTH;
              if (!same_data (old_data, new_data))
                {
                  write_modify (old_data, version);
                }
            }
          else  // new_data.filename < old_data.filename
            {
              next_read = NEW;
              write_insert (new_data, version);
            }
        }
    }
  fclose (delete_file);
  fclose (insert_file);

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

  string repo_id = bdb_ptr.get_bdb()->repo_id();
  for (string::const_iterator ri = repo_id.begin(); ri != repo_id.end(); ri++)
    {
      if (*ri != '-')
        m_repo_id += *ri;
    }
}

SQLExport::~SQLExport()
{
  // delete remaining tempfiles
  for (map<unsigned int, string>::iterator fli = filelist_map.begin(); fli != filelist_map.end(); fli++)
    {
      const string& filename = fli->second;

      if (filename != "")
        unlink (filename.c_str());
    }
}

bool
cmp_link_name (Link *l1, Link *l2)
{
  return l1->name < l2->name;
}

BDBError
SQLExport::walk (const ID& id, const ID& parent_id, const string& prefix, FILE *out_file)
{
  if (sig_interrupted)
    return BFSync::BDB_ERROR_INTR;

  INode inode = bdb_ptr.load_inode (id, version);
  if (inode.valid)
    {
      size_t written;
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

      written = fwrite (len_buffer.begin(), len_buffer.size(), 1, out_file);
      if (written != 1)
        return BFSync::BDB_ERROR_IO;

      written = fwrite (out_buffer.begin(), out_buffer.size(), 1, out_file);
      if (written != 1)
        return BFSync::BDB_ERROR_IO;

      maybe_split_transaction();
      scan_ops++;
      update_status ("scanning", false);

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

              BDBError err = walk (link->inode_id, id, inode_name, out_file);
              if (err)
                return err;
            }
        }
    }
  return BFSync::BDB_ERROR_NONE;
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
SQLExport::update_status (const string& op_name, bool force_update)
{
  const double time = gettime();
  if (fabs (time - last_status_time) > 1 || force_update)
    {
      last_status_time = time;
      printf ("\rEXPORT: version %d/%d (scanned %d entries) - %s ", status_version, status_max_version, scan_ops, op_name.c_str());
      fflush (stdout);
      // printf ("%d | %.2f scan_ops/sec\n", scan_ops, scan_ops / (time - start_time));

      if (!sig_interrupted && PyErr_CheckSignals())
        sig_interrupted = true;
    }
}

BDBError
SQLExport::build_filelist (unsigned int version, string& filename)
{
  if (filelist_map[version] != "")
    {
      filename = filelist_map[version];
      return BFSync::BDB_ERROR_NONE;
    }

  // create temp name and register it for later cleanup
  bdb_ptr.begin_transaction();

  string filelist_tmpl = string_printf ("%s/sql_export_XXXXXX", bdb_ptr.get_bdb()->get_temp_dir().c_str());
  char filelist_name_c[filelist_tmpl.size() + 1];
  strcpy (filelist_name_c, filelist_tmpl.c_str());
  int fd = mkstemp (filelist_name_c);
  assert (fd >= 0);
  close (fd);
  string filelist_name = filelist_name_c;
  bdb_ptr.add_temp_file (get_basename (filelist_name), getpid());

  bdb_ptr.commit_transaction();

  filelist_map[version] = filelist_name;

  this->version = version;
  transaction_ops = 0;
  last_status_time = 0;

  FILE *file = fopen (filelist_name.c_str(), "w");
  assert (file);

  // const double version_start_time = gettime();
  bdb_ptr.begin_transaction();
  BDBError err = walk (id_root(), id_root(), "", file);
  bdb_ptr.commit_transaction();
  // const double version_end_time = gettime();
  fclose (file);

  // printf ("### filelist time: %.2f\n", (version_end_time - version_start_time));
  // fflush (stdout);

  filename = filelist_name;
  return err;
}

void
SQLExport::export_version (unsigned int version, unsigned int max_version,
                           const string& insert_filename, const string& delete_filename)
{
  status_version = version;
  status_max_version = max_version;
  update_status ("scanning", true);

  string old_files, new_files;
  BDBError err;

  sig_interrupted = false;
  err = build_filelist (version - 1, old_files);
  if (err)
    throw BDBException (err);
  if (sig_interrupted)
    throw BDBException (BFSync::BDB_ERROR_INTR);

  err = build_filelist (version, new_files);
  if (err)
    throw BDBException (err);
  if (sig_interrupted)
    throw BDBException (BFSync::BDB_ERROR_INTR);

  // delete tempfiles we no longer need
  for (map<unsigned int, string>::iterator fli = filelist_map.begin(); fli != filelist_map.end(); fli++)
    {
      string& filename = fli->second;

      if (filename != "" && filename != old_files && filename != new_files)
        {
          unlink (filename.c_str());
          filename = "";
        }
    }

  // const double export_start_time = gettime();
  SQLExportIterator sxi = SQLExportIterator (repo_id(), old_files, new_files);
  sxi.gen_files (version, insert_filename, delete_filename);
  // const double export_end_time = gettime();

  // printf ("### export time: %.2f\n", (export_end_time - export_start_time));
  // fflush (stdout);
}

string
SQLExport::repo_id()
{
  return m_repo_id;
}

//---------------------------- SQLExportData -----------------------------

static BFSync::LeakDebugger sql_export_data_leak_debugger ("(Python)BFSync::SQLExportData");

SQLExportData::SQLExportData() :
  status (NONE),
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

static void
pg_str (string& result, const string& str, bool first = false)
{
  // escaping will at most produce twice as many chars
  // + 1 for tab
  // + 1 for \0 at end
  char buffer[str.size() * 2 + 1 + 1], *bp = buffer;
  if (!first)
    {
      *bp++ = '\t';
    }
  for (const char *si = str.c_str(); *si; si++)
    {
      if (*si == '\\')
        {
          *bp++ = '\\';
          *bp++ = '\\';
        }
      else if (*si == '\n')
        {
          *bp++ = '\\';
          *bp++ = 'n';
        }
      else if (*si == '\r')
        {
          *bp++ = '\\';
          *bp++ = 'r';
        }
      else if (*si == '\t')
        {
          *bp++ = '\\';
          *bp++ = 't';
        }
      else
        {
          *bp++ = *si;
        }
    }
  result.append (buffer, bp - buffer);
}

static void
pg_int (string& out_str, uint64_t i)
{
  // out_str += string_printf ("\t%" PRIu64, i);

  // since string_printf is slow, we use an optimized version:
  char result[64], *rp = &result[62];
  rp[1] = 0;
  for (;;)
    {
      *rp = i % 10 + '0';
      i /= 10;
      if (!i)
        {
          rp--;
          *rp = '\t';
          out_str += rp;
          return;
        }
      rp--;
    }
}

static void
pg_null (string& result)
{
  result += "\t\\N";
}

static string
x_basename (const string& filename)
{
  if (filename == "/")
    return "";
  else
    return get_basename (filename);
}

string
SQLExportData::copy_from_line (const string& repo_id_stripped, unsigned int vmin) const
{
  string result;

  pg_str (result, repo_id_stripped, true);
  pg_str (result, get_dirname (filename));
  pg_str (result, x_basename (filename));
  pg_int (result, vmin);
  pg_int (result, VERSION_INF);
  pg_str (result, id.str());

  if (id == id_root())
    pg_null (result);
  else
    pg_str (result, parent_id.str());

  pg_int (result, uid);
  pg_int (result, gid);
  pg_int (result, mode);
  pg_int (result, type);
  pg_str (result, hash);
  pg_str (result, link);
  pg_int (result, size);
  pg_int (result, major);
  pg_int (result, minor);
  pg_int (result, nlink);
  pg_int (result, ctime);
  pg_int (result, ctime_ns);
  pg_int (result, mtime);
  pg_int (result, mtime_ns);

  result += "\n";
  return result;
}

string
SQLExportData::delete_copy_from_line() const
{
  string result;
  pg_str (result, get_dirname (filename), true);
  pg_str (result, x_basename (filename));
  return result + "\n";
}
