#include <sys/time.h>
#include <assert.h>

#include <vector>

#include "bfsyncfs.hh"

using std::string;
using std::vector;

using namespace BFSync;

int
main()
{
  const char *long_path = "/usr/local/include/frob/bar.h";

  double start_t = gettime();

  for (size_t i = 0; i < 1000000; i++)
    {
      vector<string> xs = split_name (long_path);
      assert (xs.size() == 5);
    }

  double end_t = gettime();

  printf ("splits/sec:  %.f\n", 1000000 / (end_t - start_t));

  return 0;
}
