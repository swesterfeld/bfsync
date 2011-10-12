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
  static int N = 20 * 1000 * 1000;
  int k = 0;
  for (size_t i = 0; i < N; i++)
    {
      SplitPath s_path (long_path);
      while (s_path.next())
        k++;
    }

  double end_t = gettime();

  printf ("splits/sec:  %.f\n", N / (end_t - start_t));

  return 0;
}
