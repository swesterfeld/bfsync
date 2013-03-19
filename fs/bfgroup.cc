// Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

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
