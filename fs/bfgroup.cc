/*
  bfsync: Big File synchronization tool - FUSE filesystem

  Copyright (C) 2013 Stefan Westerfeld

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

#include "bfgroup.hh"
#include "bfsyncfs.hh"
#include <math.h>
#include <stdlib.h>

#include <map>

using std::string;
using std::map;

using namespace BFSync;

string
get_bfsync_group_slow (pid_t pid)
{
  string env_filename = string_printf ("/proc/%d/environ", pid);

  FILE *env_file = fopen (env_filename.c_str(), "r");
  if (!env_file)
    {
      return "";
    }

  string result = "";   // default: not found

  int c;
  do
    {
      string line;

      while ((c = getc (env_file)) > 0)
        line += c;

      if (c == 0)
        {
          size_t pos = line.find ('=');
          if (pos != string::npos)
            {
              string key = line.substr (0, pos);
              if (key == "BFSYNC_GROUP")
                {
                  result = line.substr (pos + 1);
                }
            }
        }
    }
  while (c == 0);

  fclose (env_file);

  return result;
}


namespace
{
  Mutex               get_group_mutex;
  map<pid_t, string>  get_group_cache;
  time_t              get_group_cache_time;
}

string
BFSync::get_bfsync_group (pid_t pid)
{
  Lock lock (get_group_mutex);

  /* invalidate cache every 30 seconds */
  const time_t time_now = time (NULL);
  if (labs (time_now - get_group_cache_time) > 30)
    {
      get_group_cache_time = time_now;
      get_group_cache.clear();
    }

  map<pid_t, string>::iterator gi = get_group_cache.find (pid);
  if (gi != get_group_cache.end())
    return gi->second;

  string group = get_bfsync_group_slow (pid);
  get_group_cache[pid] = group;
  return group;
}
