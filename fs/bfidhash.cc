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
#include <stdio.h>

using std::string;

namespace BFSync {

static unsigned char
hex_nibble (char c)
{
  int uc = (unsigned char)c;

  if (uc >= '0' && uc <= '9') return uc - (unsigned char) '0';
  if (uc >= 'a' && uc <= 'f') return uc + 10 - (unsigned char) 'a';
  if (uc >= 'A' && uc <= 'F') return uc + 10 - (unsigned char) 'A';

  return 16;      // error
}

static guint32
hex2uint32 (const char *hex)
{
  return
    (hex_nibble (hex[0]) << 28) + (hex_nibble (hex[1]) << 24) +
    (hex_nibble (hex[2]) << 20) + (hex_nibble (hex[3]) << 16) +
    (hex_nibble (hex[4]) << 12) + (hex_nibble (hex[5]) << 8) +
    (hex_nibble (hex[6]) << 4)  + (hex_nibble (hex[7]));
}

ID::ID (const string& str)
{
  g_return_if_fail (str.size() == 40);

  const char *sp = str.c_str();
  a = hex2uint32 (sp);
  b = hex2uint32 (sp + 8);
  c = hex2uint32 (sp + 16);
  d = hex2uint32 (sp + 24);
  e = hex2uint32 (sp + 32);
}

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

ID::ID()
{
}

ID
ID::gen_new()
{
  ID result;

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

}
