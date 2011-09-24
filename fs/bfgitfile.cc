/*
  bfsync: Big File synchronization based on Git

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

#include "bfgitfile.hh"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

using std::string;

GitFile::GitFile() :
  size (0)
{
}

bool
GitFile::parse (const string& filename)
{
  printf ("parse => %s\n", filename.c_str());

  FILE *file = fopen (filename.c_str(), "r");
  if (!file)
    return false;

  bool result = true;
  size_t size_count = 0, hash_count = 0, mtime_count = 0, mtime_ns_count = 0, link_count = 0, type_count = 0;
  size_t uid_count = 0, gid_count = 0, mode_count = 0, major_count = 0, minor_count = 0;
  char buffer[1024];
  while (fgets (buffer, 1024, file))
    {
      char *key = strtok (buffer, " \n");
      if (key)
        {
          char *eq  = strtok (NULL, " \n");
          if (eq)
            {
              char *val = strtok (NULL, " \n");
              if (val && string (eq) == "=")
                {
                  if (string (key) == "size")
                    {
                      size = atoi (val);
                      size_count++;
                      printf ("size (%s) => %zd\n", filename.c_str(), size);
                    }
                  else if (string (key) == "hash")
                    {
                      hash = val;
                      hash_count++;
                      printf ("hash (%s) => %s\n", filename.c_str(), hash.c_str());
                    }
                  else if (string (key) == "mtime")
                    {
                      mtime = atol (val);
                      mtime_count++;
                    }
                  else if (string (key) == "mtime_ns")
                    {
                      mtime_ns = atoi (val);
                      mtime_ns_count++;
                    }
                  else if (string (key) == "uid")
                    {
                      uid = atoi (val);
                      uid_count++;
                    }
                  else if (string (key) == "gid")
                    {
                      gid = atoi (val);
                      gid_count++;
                    }
                  else if (string (key) == "link")
                    {
                      link = val;
                      link_count++;
                    }
                  else if (string (key) == "mode")
                    {
                      mode = strtol (val, NULL, 8);
                      mode_count++;
                    }
                  else if (string (key) == "major")
                    {
                      major = atoi (val);
                      major_count++;
                    }
                  else if (string (key) == "minor")
                    {
                      minor = atoi (val);
                      minor_count++;
                    }
                  else if (string (key) == "type")
                    {
                      if (string (val) == "file")
                        type = FILE_REGULAR;
                      else if (string (val) == "symlink")
                        type = FILE_SYMLINK;
                      else if (string (val) == "dir")
                        type = FILE_DIR;
                      else if (string (val) == "fifo")
                        type = FILE_FIFO;
                      else if (string (val) == "socket")
                        type = FILE_SOCKET;
                      else if (string (val) == "blockdev")
                        type = FILE_BLOCK_DEV;
                      else if (string (val) == "chardev")
                        type = FILE_CHAR_DEV;
                      else
                        type = FILE_NONE;
                      type_count++;
                    }
                }
            }
        }
    }
  if (type_count != 1)
    result = false;
  if (type == FILE_REGULAR)
    {
      if (size_count != 1)
        result = false;
      if (hash_count != 1)
        result = false;
    }
  if (type == FILE_BLOCK_DEV || type == FILE_CHAR_DEV)
    {
      if (major_count != 1 || minor_count != 1)
        result = false;
    }
  if (mode_count != 1)
    result = false;
  if (mtime_count != 1 && mtime_ns_count != 1)
    result = false;
  if (uid_count != 1)
    result = false;
  if (gid_count != 1)
    result = false;
  fclose (file);
  return result;
}

bool
GitFile::save (const string& filename)
{
  if (type == FILE_REGULAR)
    {
      FILE *file = fopen (filename.c_str(), "w");
      if (!file)
        return false;

      fprintf (file, "type = file\n");
      fprintf (file, "hash = %s\n", hash.c_str());
      fprintf (file, "size = %zd\n", size);
      fprintf (file, "uid = %d\n", uid);
      fprintf (file, "gid = %d\n", gid);
      fprintf (file, "mode = %o\n", mode);
      fprintf (file, "mtime = %ld\n", mtime);
      fprintf (file, "mtime_ns = %d\n", mtime_ns);
      fclose (file);
      return true;
    }
  return false;
}


