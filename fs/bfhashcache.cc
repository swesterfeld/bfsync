// Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

#include "bfsyncdb.hh"
#include "bfleakdebugger.hh"

using std::string;

using BFSync::DataOutBuffer;
using BFSync::DataBuffer;
using BFSync::BDBError;

//----------------------- HashCacheEntry --------------------------

static BFSync::LeakDebugger hash_cache_entry_leak_debugger ("(Python)BFSync::HashCacheEntry");

HashCacheEntry::HashCacheEntry() :
  valid (false),
  expire_time (0)
{
  hash_cache_entry_leak_debugger.add (this);
}

HashCacheEntry::HashCacheEntry (const HashCacheEntry& hce) :
  valid (hce.valid),
  stat_hash (hce.stat_hash),
  file_hash (hce.file_hash),
  expire_time (hce.expire_time)
{
  hash_cache_entry_leak_debugger.add (this);
}

HashCacheEntry::~HashCacheEntry()
{
  hash_cache_entry_leak_debugger.del (this);
}

//----------------------- HashCacheDict --------------------------

static inline bool
operator== (const HashCacheDict::DictKey& x, const HashCacheDict::DictKey& y)
{
  return (x.a == y.a) && (x.b == y.b) && (x.c == y.c) && (x.d == y.d) && (x.e == y.e);
}

DataOutBuffer
magic_buffer()
{
  DataOutBuffer buffer;

  const int version = 1;
  const int endianness = 0x12345678;

  buffer.write_string ("bfsync HashCache\n");
  buffer.write_uint32 (version);
  buffer.write_uint32 (endianness);
  return buffer;
}

void
HashCacheDict::insert (const string& stat_hash, const string& file_hash, unsigned int expire_time)
{
  DataOutBuffer buffer;
  buffer.write_hash (stat_hash);
  buffer.write_hash (file_hash);
  buffer.write_uint32 (expire_time);
  assert (buffer.size() == 44);

  DictKey key;
  DictValue value;

  const char *bptr = buffer.begin();

  assert (sizeof (key) == 20);
  memcpy (&key, bptr, 20);
  assert (sizeof (value) == 24);
  memcpy (&value, bptr + 20, 24);

  hc_dict[key] = value;
}

HashCacheEntry
HashCacheDict::lookup (const string& stat_hash)
{
  DataOutBuffer buffer;
  buffer.write_hash (stat_hash);
  assert (buffer.size() == 20);

  DictKey key;

  const char *bptr = buffer.begin();

  assert (sizeof (key) == 20);
  memcpy (&key, bptr, 20);

  boost::unordered_map<DictKey, DictValue>::const_iterator hi = hc_dict.find (key);
  if (hi == hc_dict.end())
    {
      HashCacheEntry not_found;
      return not_found;
    }
  DataBuffer dbuffer ((char *) &hi->second, 24);

  HashCacheEntry result;
  result.valid = true;
  result.stat_hash = stat_hash;
  result.file_hash = dbuffer.read_hash();
  result.expire_time = dbuffer.read_uint32();
  return result;
}

void
HashCacheDict::save (const string& filename)
{
  FILE *file = fopen (filename.c_str(), "w");
  if (!file)
    {
      throw BDBException (BFSync::BDB_ERROR_IO);
    }

  BDBError err = BFSync::BDB_ERROR_NONE;
  size_t io_err;

  DataOutBuffer mbuffer = magic_buffer();
  io_err = fwrite (mbuffer.begin(), mbuffer.size(), 1, file);

  if (io_err != 1)
    {
      err = BFSync::BDB_ERROR_IO;
    }

  if (!err)
    {
      boost::unordered_map<DictKey, DictValue>::iterator hdi;

      for (hdi = hc_dict.begin(); hdi != hc_dict.end(); hdi++)
        {
          assert (sizeof (hdi->first) == 20);
          io_err = fwrite (&hdi->first, sizeof (hdi->first), 1, file);
          if (io_err != 1)
            {
              err = BFSync::BDB_ERROR_IO;
              break;
            }

          assert (sizeof (hdi->second) == 24);
          io_err = fwrite (&hdi->second, sizeof (hdi->second), 1, file);
          if (io_err != 1)
            {
              err = BFSync::BDB_ERROR_IO;
              break;
            }
        }
    }

  int fcresult;
  fcresult = fclose (file);
  if (fcresult != 0)
    err = BFSync::BDB_ERROR_IO;

  if (err)
    {
      throw BDBException (err);
    }
}

void
HashCacheDict::load (const string& filename, unsigned int load_time)
{
  FILE *file = fopen (filename.c_str(), "r");
  if (!file)
    {
      throw BDBException (BFSync::BDB_ERROR_IO);
    }

  BDBError err = BFSync::BDB_ERROR_NONE;

  DataOutBuffer mbuffer = magic_buffer();
  char file_magic[mbuffer.size()];
  size_t io_err = fread (file_magic, mbuffer.size(), 1, file);
  if (io_err != 1)
    {
      err = BFSync::BDB_ERROR_IO;
    }
  if (!err && memcmp (file_magic, mbuffer.begin(), mbuffer.size()) != 0)
    {
      fprintf (stderr, "bfsync: HashCache file format not compatible: %s\n", filename.c_str());
      err = BFSync::BDB_ERROR_IO;
    }

  if (!err)
    {
      DictKey key;
      DictValue value;
      assert (sizeof (key) == 20);
      assert (sizeof (value) == 24);
      for (;;)
        {
          io_err = fread (&key, sizeof (key), 1, file);
          if (io_err != 1)
            {
              if (ferror (file))
                err = BFSync::BDB_ERROR_IO;

              break;
            }

          io_err = fread (&value, sizeof (value), 1, file);
          if (io_err != 1)
            {
              err = BFSync::BDB_ERROR_IO;
              break;
            }
          if (load_time < value.expire_time)
            {
              hc_dict[key] = value;
            }
        }
    }

  int fcresult;
  fcresult = fclose (file);
  if (fcresult != 0)
    err = BFSync::BDB_ERROR_IO;

  if (err)
    {
      throw BDBException (err);
    }
}

//----------------------- HashCacheIterator --------------------------

HashCacheIterator::HashCacheIterator (HashCacheDict& dict) :
  dict (dict)
{
  it = dict.hc_dict.begin();
}

HashCacheEntry
HashCacheIterator::get_next()
{
  if (it == dict.hc_dict.end())
    {
      HashCacheEntry not_found;
      return not_found;
    }

  DataBuffer key_dbuffer ((char *) &it->first, 20);
  DataBuffer value_dbuffer ((char *) &it->second, 24);

  HashCacheEntry result;
  result.valid = true;
  result.stat_hash = key_dbuffer.read_hash();
  result.file_hash = value_dbuffer.read_hash();
  result.expire_time = value_dbuffer.read_uint32();

  it++;
  return result;
}
