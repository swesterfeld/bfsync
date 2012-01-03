/*
  bfsync: Big File synchronization tool - FUSE filesystem

  Copyright (C) 2011 Stefan Westerfeld

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

#include "bfidhash.hh"
#include "bfbdb.hh"
#include <stdio.h>

using std::string;

namespace BFSync {

static void
uint32_hex (guint32 value, char *dest)
{
  const char *hex_nibble = "0123456789abcdef";

  dest[0] = hex_nibble [(value >> 28) & 0xf];
  dest[1] = hex_nibble [(value >> 24) & 0xf];
  dest[2] = hex_nibble [(value >> 20) & 0xf];
  dest[3] = hex_nibble [(value >> 16) & 0xf];
  dest[4] = hex_nibble [(value >> 12) & 0xf];
  dest[5] = hex_nibble [(value >> 8) & 0xf];
  dest[6] = hex_nibble [(value >> 4) & 0xf];
  dest[7] = hex_nibble [value  & 0xf];
}

string
ID::str() const
{
  char buffer[41];

  uint32_hex (a, buffer);
  uint32_hex (b, buffer + 8);
  uint32_hex (c, buffer + 16);
  uint32_hex (d, buffer + 24);
  uint32_hex (e, buffer + 32);
  buffer[40] = 0;

  return buffer;
}

string
ID::pretty_str() const
{
  char buffer[45];

  uint32_hex (a, buffer);
  buffer[8] = '-';
  uint32_hex (b, buffer + 9);
  buffer[17] = '-';
  uint32_hex (c, buffer + 18);
  buffer[26] = '-';
  uint32_hex (d, buffer + 27);
  buffer[35] = '-';
  uint32_hex (e, buffer + 36);
  buffer[44] = 0;

  string prefix;
  for (size_t i = 0; i < path_prefix.size(); i++)
    prefix += string_printf ("%02x", (unsigned char) path_prefix[i]);

  return prefix + "/" + buffer;
}

ID::ID()
{
}

ID
ID::gen_new (const char *path)
{
  ID result;

  SplitPath sp (path);

  const char *p;
  while ((p = sp.next()))
    result.path_prefix.push_back (g_str_hash (p) % 255 + 1);
  result.path_prefix.pop_back();

  result.a = g_random_int();
  result.b = g_random_int();
  result.c = g_random_int();
  result.d = g_random_int();
  result.e = g_random_int();

  return result;
}

ID
ID::root()
{
  ID result;

  result.a = 0;
  result.b = 0;
  result.c = 0;
  result.d = 0;
  result.e = 0;

  return result;
}

void
ID::store (DataOutBuffer& data_buf) const
{
  data_buf.write_vec_zero (path_prefix);
  data_buf.write_uint32 (a);
  data_buf.write_uint32 (b);
  data_buf.write_uint32 (c);
  data_buf.write_uint32 (d);
  data_buf.write_uint32 (e);
}

ID::ID (DataBuffer& data_buf)
{
  data_buf.read_vec_zero (path_prefix);
  a = data_buf.read_uint32();
  b = data_buf.read_uint32();
  c = data_buf.read_uint32();
  d = data_buf.read_uint32();
  e = data_buf.read_uint32();
}

}
