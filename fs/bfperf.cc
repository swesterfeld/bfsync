#include <sys/time.h>
#include <assert.h>

#include <vector>
#include <map>

#include "bfsyncfs.hh"

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

  printf ("splits/sec:  %10.f\n", N / (end_t - start_t));
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

  printf ("id-str/sec:  %10.f\n", N / (end_t - start_t));
}

struct ID
{
  guint32 a, b, c, d, e;
};

inline bool
operator< (const ID& x, const ID& y)
{
  if (x.a != y.a)
    return x.a < y.a;

  if (x.b != y.b)
    return x.b < y.b;

  if (x.c != y.c)
    return x.c < y.c;

  if (x.d != y.d)
    return x.d < y.d;

  return x.e < y.e;
}

inline bool
operator== (const ID& x, const ID& y)
{
  return (x.a == y.a) && (x.b == y.b) && (x.c == y.c) && (x.d == y.d) && (x.e == y.e);
}

ID
gen_id()
{
  ID result;
  result.a = g_random_int();
  result.b = g_random_int();
  result.c = g_random_int();
  result.d = g_random_int();
  result.e = g_random_int();
  return result;
}

void
perf_id()
{
  vector<ID> ids;

  for (size_t i = 0; i < 300000; i++)   // 300000 ~= crude estimate for average files on a linux system
    ids.push_back (gen_id());

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

  printf ("id/sec:      %10.f\n", N / (end_t - start_t));
}

void
perf_id_hash()
{
  vector<ID> ids;

  for (size_t i = 0; i < 300000; i++)   // 300000 ~= crude estimate for average files on a linux system
    ids.push_back (gen_id());

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

  printf ("hash_id/sec: %10.f\n", N / (end_t - start_t));
}

int
main()
{
  perf_split();
  perf_id_str();
  perf_id();
  perf_id_hash();

  return 0;
}
