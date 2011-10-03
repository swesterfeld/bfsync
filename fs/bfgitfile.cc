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
#include "bfsyncfs.hh"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <vector>
#include <algorithm>

using std::string;
using std::vector;

namespace BFSync {

GitFilePtr::GitFilePtr (const string& filename, Mode mode)
{
  ptr = new GitFile;

  string git_filename = Options::the()->repo_path + "/git/files/" + name2git_name (filename);
  if (mode == LOAD)
    {
      if (!ptr->parse (git_filename))
        {
          delete ptr;
          ptr = NULL;
        }
    }
  else if (mode == NEW)
    {
      ptr = new GitFile();
      ptr->git_filename = git_filename;
      ptr->uid = getuid();
      ptr->gid = getgid();
      ptr->set_mtime_ctime_now();
      ptr->updated = true;
    }
}

GitFilePtr::~GitFilePtr()
{
  if (ptr)
    {
      if (ptr->updated)
        {
          ptr->save (ptr->git_filename);
        }
      delete ptr;
      ptr = NULL;
    }
}

GitFile::GitFile() :
  size      (0),
  mtime     (0),
  mtime_ns  (0),
  ctime     (0),
  ctime_ns  (0),
  uid       (0),
  gid       (0),
  mode      (0),
  type      (FILE_NONE),
  updated   (false)
{
  major = minor = 0;
}

bool
GitFile::parse (const string& filename)
{
  git_filename = filename;

  printf ("parse => %s\n", filename.c_str());

  FILE *file = fopen (filename.c_str(), "r");
  if (!file)
    return false;

  bool result = true;
  size_t size_count = 0, hash_count = 0, mtime_count = 0, mtime_ns_count = 0, link_count = 0, type_count = 0;
  size_t uid_count = 0, gid_count = 0, mode_count = 0, major_count = 0, minor_count = 0;
  size_t ctime_count = 0, ctime_ns_count = 0;
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
                  else if (string (key) == "ctime")
                    {
                      ctime = atol (val);
                      ctime_count++;
                    }
                  else if (string (key) == "ctime_ns")
                    {
                      ctime_ns = atoi (val);
                      ctime_ns_count++;
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
  if (ctime_count != 1 && ctime_ns_count != 1)
    result = false;
  if (uid_count != 1)
    result = false;
  if (gid_count != 1)
    result = false;
  fclose (file);

  return result;
}

struct Attributes
{
  vector<string> attrs;

  void
  add (const string& attrib, const string& value)
  {
    attrs.push_back (attrib + " = " + value);
  }
  void
  add (const string& attrib, int64_t value)
  {
    char buffer[64];
    sprintf (buffer, "%s = %ld", attrib.c_str(), value);
    attrs.push_back (buffer);
  }
  void
  add_oct (const string& attrib, int64_t value)
  {
    char buffer[64];
    sprintf (buffer, "%s = %lo", attrib.c_str(), value);
    attrs.push_back (buffer);
  }
  void write (FILE *file);
};

void
Attributes::write (FILE *file)
{
  std::sort (attrs.begin(), attrs.end());

  for (vector<string>::iterator ai = attrs.begin(); ai != attrs.end(); ai++)
    {
      fprintf (file, "%s\n", ai->c_str());
    }
}

bool
GitFile::save (const string& filename)
{
  Attributes attributes;

  attributes.add ("uid", uid);
  attributes.add ("gid", gid);
  attributes.add_oct ("mode", mode);
  attributes.add ("mtime", mtime);
  attributes.add ("mtime_ns", mtime_ns);
  attributes.add ("ctime", ctime);
  attributes.add ("ctime_ns", ctime_ns);

  if (type == FILE_REGULAR)
    {
      attributes.add ("type", "file");
      attributes.add ("hash", hash);
      attributes.add ("size", size);
    }
  else if (type == FILE_DIR)
    {
      attributes.add ("type", "dir");
    }
  else if (type == FILE_SYMLINK)
    {
      attributes.add ("type", "symlink");
      attributes.add ("link", link);
    }
  else if (type == FILE_FIFO)
    {
      attributes.add ("type", "fifo");
    }
  else if (type == FILE_SOCKET)
    {
      attributes.add ("type", "socket");
    }
  else if (type == FILE_BLOCK_DEV || type == FILE_CHAR_DEV)
    {
      if (type == FILE_BLOCK_DEV)
        attributes.add ("type", "blockdev");
      else // filetype == FILE_CHAR_DEV
        attributes.add ("type", "chardev");

      attributes.add ("major", major);
      attributes.add ("minor", minor);
    }
  else // unsupported type
    {
      return false;
    }

  FILE *file = fopen (filename.c_str(), "w");
  if (!file)
    return false;

  attributes.write (file);

  fclose (file);
  return true;
}

void
GitFile::set_mtime_ctime_now()
{
  timespec time_now;

  if (clock_gettime (CLOCK_REALTIME, &time_now) == 0)
    {
      mtime     = time_now.tv_sec;
      mtime_ns  = time_now.tv_nsec;
      ctime     = time_now.tv_sec;
      ctime_ns  = time_now.tv_nsec;
    }
}

void
GitFile::set_ctime_now()
{
  timespec time_now;

  if (clock_gettime (CLOCK_REALTIME, &time_now) == 0)
    {
      ctime     = time_now.tv_sec;
      ctime_ns  = time_now.tv_nsec;
    }
}

}
