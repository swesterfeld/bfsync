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
#include "bfleakdebugger.hh"

using std::string;

using BFSync::DataOutBuffer;
using BFSync::DataBuffer;

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
