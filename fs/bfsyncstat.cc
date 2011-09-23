#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <string>

using std::string;

int
main (int argc, char **argv)
{
  string str;
  for (int i = 1; i < argc; i++)
    {
      struct stat st;

      lstat (argv[i], &st);
      printf ("%s", argv[i]);
      putchar (0);
      printf ("%ld", st.st_mtim.tv_sec);
      putchar (0);
      printf ("%ld", st.st_mtim.tv_nsec);
      putchar (0);
    }
}
