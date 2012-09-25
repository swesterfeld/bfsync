/*
  bfsync: Big File synchronization tool

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

#define __STDC_FORMAT_MACROS 1

#include <sys/time.h>
#include <assert.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <vector>
#include <map>
#include <sstream>
#include <boost/lexical_cast.hpp>

#include "bfsyncfs.hh"
#include "bfidhash.hh"
#include "bfbdb.hh"
#include "bfleakdebugger.hh"

using std::string;
using std::vector;
using std::map;
using std::stringstream;

using namespace BFSync;

static string
gen_id_str()
{
  string id = "bfbdb000/";
  // globally (across all versions/hosts) uniq id, with the same amount of information as a SHA1-hash
  for (size_t i = 0; i < 5; i++)
    {
      char hex[32];
      sprintf (hex, "%08x", g_random_int());
      id += hex;
    }
  return id;
}

static void
print_result (const string& name, double value)
{
  printf ("%-15s%10.f\n", (name + ":").c_str(), value);
}

void
perf_split()
{
  const char *long_path = "/usr/local/include/frob/bar.h";

  double start_t = gettime();
  static size_t N = 20 * 1000 * 1000;
  int k = 0;
  for (size_t i = 0; i < N; i++)
    {
      SplitPath s_path (long_path);
      while (s_path.next())
        k++;
    }

  double end_t = gettime();

  print_result ("splits/sec", N / (end_t - start_t));
}

void
perf_id_str()
{
  vector<string>  ids;
  for (size_t i = 0; i < 300000; i++)   // 300000 ~= crude estimate for average files on a linux system
    ids.push_back (gen_id_str());

  map<string,int> id_map;
  for (size_t i = 0; i < ids.size(); i++)
    id_map[ids[i]] = i;

  const double start_t = gettime();
  const size_t N = 1 * 1000 * 1000;

  for (size_t i = 0; i < N; i++)
    {
      int search = g_random_int_range (0, ids.size());
      if (id_map[ids[search]] != search)
        assert (false);
    }

  const double end_t = gettime();

  print_result ("id-str/sec", N / (end_t - start_t));
}

void
perf_id()
{
  vector<ID> ids;

  for (size_t dir = 0; dir < 3000; dir++)
    {
      string dir_str = string_printf ("/dir%zd/foo", dir);

      for (size_t i = 0; i < 100; i++)   // 300000 ~= crude estimate for average files on a linux system
        ids.push_back (ID::gen_new (dir_str.c_str()));
    }

  map<ID,int> id_map;
  for (size_t i = 0; i < ids.size(); i++)
    id_map[ids[i]] = i;

  const double start_t = gettime();
  const size_t N = 3 * 1000 * 1000;
  for (size_t i = 0; i < N; i++)
    {
      int search = g_random_int_range (0, ids.size());
      if (id_map[ids[search]] != search)
        assert (false);
    }

  const double end_t = gettime();

  print_result ("id/sec", N / (end_t - start_t));
}

void
perf_id_hash()
{
  vector<ID> ids;

  for (size_t i = 0; i < 300000; i++)   // 300000 ~= crude estimate for average files on a linux system
    ids.push_back (ID::gen_new ("/foo/par"));

  vector< vector<ID> > hmap (49999); // prime
  for (size_t i = 0; i < ids.size(); i++)
    hmap[ids[i].b % 49999].push_back (ids[i]);

  const double start_t = gettime();
  const size_t N = 3 * 1000 * 1000;
  for (size_t i = 0; i < N; i++)
    {
      int search = i % ids.size();
      const ID& need_id = ids[search];
      const vector<ID>& v_bucket = hmap[need_id.b % hmap.size()];

      bool found = false;
      for (vector<ID>::const_iterator hmi = v_bucket.begin(); hmi != v_bucket.end(); hmi++)
        {
          if (*hmi == need_id)
            {
              found = true;
              break;
            }
        }
      assert (found);
    }

  const double end_t = gettime();

  print_result ("hash_id/sec", N / (end_t - start_t));
}

void
perf_getattr()
{
  int sysrc;

  sysrc = system ("mkdir -p mnt/xtest/ytest/ztest");
  assert (WEXITSTATUS (sysrc) == 0);

  sysrc = system ("touch mnt/xtest/ytest/ztest/foo");
  assert (WEXITSTATUS (sysrc) == 0);

  struct stat st;

  // stat on builtin file
  double start_t = gettime();
  const size_t NI = 300 * 1000;
  for (size_t i = 0; i < NI; i++)
    {
      int r = stat ("mnt/.bfsync/info", &st);
      assert (r == 0);
    }
  double end_t = gettime();

  print_result ("info_stat/sec", NI / (end_t - start_t));

  // stat on real file
  start_t = gettime();

  const size_t N = 300 * 1000;
  for (size_t i = 0; i < N; i++)
    {
      int r = stat ("mnt/xtest/ytest/ztest/foo", &st);
      assert (r == 0);
    }

  end_t = gettime();

  print_result ("stat/sec", N / (end_t - start_t));
}

void
perf_str2id()
{
  vector<string> ids;
  for (size_t i = 0; i < 300000; i++)   // 300000 ~= crude estimate for average files on a linux system
    ids.push_back (gen_id_str());

  const double start_t = gettime();
  const size_t N = 1000 * 1000;
  for (size_t i = 0; i < N; i++)
    {
      BFSync::ID id (ids[i % ids.size()]);
    }
  const double end_t = gettime();

  print_result ("str2id/sec", N / (end_t - start_t));
}

void
perf_id2str()
{
  vector<ID> ids;
  for (size_t i = 0; i < 300000; i++)   // 300000 ~= crude estimate for average files on a linux system
    ids.push_back (ID (gen_id_str()));

  const double start_t = gettime();
  const size_t N = 1000 * 1000;
  for (size_t i = 0; i < N; i++)
    {
      string sid = ids[i % ids.size()].str();
    }
  const double end_t = gettime();

  print_result ("id2str/sec", N / (end_t - start_t));
}

void
perf_read_string()
{
  string s;
  for (size_t i = 0; i < 40; i++)
    s += "x";
  DataOutBuffer db;
  db.write_string (s);
  vector<char> data (db.data());

  const double start_t = gettime();
  const size_t N = 10 * 1000 * 1000;
  for (size_t i = 0; i < N; i++)
    {
      DataBuffer dbuffer (&data[0], data.size());
      string rs = dbuffer.read_string();
      assert (rs == s);
    }
  const double end_t = gettime();

  print_result ("read_str/sec", N / (end_t - start_t));
}

static LeakDebugger x_leak_debugger ("test");

void
perf_leak_debugger()
{
  const size_t N = 1000 * 1000;

  vector<void *> ptrs;
  for (size_t i = 0; i < N; i++)
    {
      void *ptr = (void *) (size_t) (i * 16);
      ptrs.push_back (ptr);
    }
  std::random_shuffle (ptrs.begin(), ptrs.end());

  const double start_t = gettime();
  for (vector<void *>::iterator pi = ptrs.begin(); pi != ptrs.end(); pi++)
    x_leak_debugger.add (*pi);
  for (vector<void *>::iterator pi = ptrs.begin(); pi != ptrs.end(); pi++)
    x_leak_debugger.del (*pi);
  const double end_t = gettime();

  print_result ("leak_deb/sec", N / (end_t - start_t));
}

string
int2str (uint64_t i)
{
  return string_printf ("%" PRIu64, i);
}

stringstream ss;

string
int2str2 (uint64_t i)
{
  ss.str("");
  ss << i;
  return ss.str();
}

string
int2str3 (uint64_t i)
{
  return boost::lexical_cast<string> (i);
}

string
int2str4 (uint64_t i)
{
  char result[64], *rp = &result[62];
  rp[1] = 0;
  for (;;)
    {
      *rp = i % 10 + '0';
      i /= 10;
      if (!i)
        return rp;
      rp--;
    }
}

void
perf_int2str()
{
  const uint64_t N = 1000 * 1000;
  double start_t, end_t;
  string s;

  // int2str
  start_t = gettime();
  for (uint64_t i = 0; i < N; i++)
    {
      s = int2str (i);
    }
  assert (int2str (12345678) == "12345678");
  end_t = gettime();

  print_result ("int2str/sec", N / (end_t - start_t));

  // int2str2
  start_t = gettime();
  for (uint64_t i = 0; i < N; i++)
    {
      s = int2str2 (i);
    }
  assert (int2str2 (12345678) == "12345678");
  end_t = gettime();

  print_result ("int2str2/sec", N / (end_t - start_t));

  // int2str3
  start_t = gettime();
  for (uint64_t i = 0; i < N; i++)
    {
      s = int2str3 (i);
    }
  assert (int2str3 (12345678) == "12345678");
  end_t = gettime();

  print_result ("int2str3/sec", N / (end_t - start_t));

  // int2str4
  start_t = gettime();
  for (uint64_t i = 0; i < N; i++)
    {
      s = int2str4 (i);
    }
  assert (int2str4 (12345678) == "12345678");
  assert (int2str4 (18446744073709551615ULL) == "18446744073709551615");
  end_t = gettime();

  print_result ("int2str4/sec", N / (end_t - start_t));
}

int
main()
{
  perf_split();
  perf_id_str();
  perf_id();
  perf_id_hash();
  perf_str2id();
  perf_id2str();
  perf_read_string();
  perf_leak_debugger();
  perf_int2str();
  FILE *test = fopen ("mnt/.bfsync/info", "r");
  if (!test)
    {
      printf ("bfsyncfs not mounted => no tests started\n");
      return 0;
    }
  perf_getattr();
  return 0;
}
