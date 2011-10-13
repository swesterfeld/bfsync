#include <sys/time.h>
#include <assert.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <vector>
#include <map>

#include "bfsyncfs.hh"
#include "bfidhash.hh"

using std::string;
using std::vector;
using std::map;

using namespace BFSync;

static string
gen_id_str()
{
  string id;
  // globally (across all versions/hosts) uniq id, with the same amount of information as a SHA1-hash
  while (id.size() < 40)
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
  printf ("%-12s%10.f\n", (name + ":").c_str(), value);
}

void
perf_split()
{
  const char *long_path = "/usr/local/include/frob/bar.h";

  double start_t = gettime();
  static int N = 20 * 1000 * 1000;
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

  for (size_t i = 0; i < 300000; i++)   // 300000 ~= crude estimate for average files on a linux system
    ids.push_back (ID::gen_new());

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
    ids.push_back (ID::gen_new());

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
  system ("mkdir -p mnt/xtest/ytest/ztest");
  system ("touch mnt/xtest/ytest/ztest/foo");

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


int
main()
{
  perf_split();
  perf_id_str();
  perf_id();
  perf_id_hash();
  perf_str2id();
  perf_id2str();
  FILE *test = fopen ("mnt/.bfsync/info", "r");
  if (!test)
    {
      printf ("bfsyncfs not mounted => no tests started\n");
      return 0;
    }
  perf_getattr();
  return 0;
}
