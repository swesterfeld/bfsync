// Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

#include "bfsyncserver.hh"
#include "bfsyncfs.hh"
#include "bfinode.hh"
#include "bfsyncfs.hh"
#include "bfhistory.hh"
#include "bfbdb.hh"
#include "bftimeprof.hh"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <poll.h>
#include <errno.h>
#include <stdlib.h>
#include <glib.h>
#include <math.h>

#include <string>
#include <vector>

using namespace BFSync;

using std::string;
using std::vector;

Server::Server() :
  socket_ok (false),
  socket_fd (-1),
  thread_running (false)
{
}

Server::~Server()
{
  assert (!thread_running);

  if (socket_ok)
    {
      close (socket_fd);
      unlink (socket_path.c_str());
    }
}

static void*
thread_start (void *arg)
{
  Server *instance = static_cast<Server *> (arg);
  instance->run();
  return NULL;
}

bool
Server::init_socket (const string& repo_path)
{
  assert (!socket_ok);

  // if pid file is there, and pid does not exist anymore, remove old socket file
  FILE *pid_file = fopen ((Options::the()->repo_path + "/pid").c_str(), "r");
  if (pid_file)
    {
      char buffer[64];
      if (fgets (buffer, 64, pid_file))
        {
          int pid = atoi (buffer);
          if (kill (pid, 0) == -1 && errno == ESRCH)
            {
              unlink ((Options::the()->repo_path + "/socket").c_str());
            }
        }
      fclose (pid_file);
    }

  socket_fd = socket (PF_UNIX, SOCK_STREAM, 0);
  if (socket_fd < 0)
    {
      printf ("bfsyncfs: creation of unix domain socket failed\n");
      return false;
    }

  if (fcntl (socket_fd, F_SETFL, O_NONBLOCK) < 0)
    {
      printf ("bfsyncfs: can't initialize non blocking I/O on socket\n");
      close (socket_fd);
      return false;
    }

  socket_path = repo_path + "/socket";

  struct sockaddr_un socket_addr;

  int maxlen = sizeof (socket_addr.sun_path);
  strncpy (socket_addr.sun_path, socket_path.c_str(), maxlen);
  socket_addr.sun_path[maxlen-1] = 0;
  socket_addr.sun_family = AF_UNIX;

  if (bind (socket_fd, (struct sockaddr *) &socket_addr, sizeof (struct sockaddr_un)) < 0)
    {
      printf ("bfsyncfs: can't bind socket to file %s\n", socket_path.c_str());
      close (socket_fd);
      return false;
    }

  if (listen (socket_fd, 16) < 0)
    {
      printf ("bfsyncfs: can't listen on the socket");
      close (socket_fd);
      return false;
    }
  socket_ok = true;
  return true;
}

void
Server::start_thread()
{
  assert (!thread_running);

  if (socket_ok)
    {
      pthread_create (&thread, NULL, thread_start, this);

      int pipe_ok = pipe (wakeup_pipe_fds);
      assert (pipe_ok == 0);

      thread_running = true;
    }
}

void
Server::stop_thread()
{
  if (thread_running)
    {
      while (write (wakeup_pipe_fds[1], "W", 1) != 1)
        ;

      void *result;
      pthread_join (thread, &result);
      thread_running = false;
    }
}

void
Server::run()
{
  debug ("Server::run()\n");

  FILE *pid_file = fopen ((Options::the()->repo_path + "/pid").c_str(), "w");
  if (pid_file)
    {
      fprintf (pid_file, "%d\n", getpid());
      fclose (pid_file);
    }

  struct pollfd poll_fds[2];

  poll_fds[0].fd = socket_fd;
  poll_fds[0].events = POLLIN;
  poll_fds[0].revents = 0;

  poll_fds[1].fd = wakeup_pipe_fds[0];
  poll_fds[1].events = POLLIN;
  poll_fds[1].revents = 0;

  while (1)
    {
      if (poll (poll_fds, 2, 5000) > 0)
        {
          if (poll_fds[1].revents)
            {
              // filesystem is terminating -> exit from this thread
              return;
            }

          if (poll_fds[0].revents)
            {
              struct sockaddr_un incoming;
              socklen_t size_in = sizeof (struct sockaddr_un);

              int client_fd = accept (socket_fd, (struct sockaddr*) &incoming, &size_in);
              if (client_fd > 0)
                {
                  debug ("Server: handle_client (%d)\n", client_fd);
                  handle_client (client_fd);
                }
            }
        }
      else  // timeout
        {
          FSLock lock (FSLock::WRITE); // we don't want anybody to modify stuff while we write

          INodeRepo::the()->save_changes();
          INodeRepo::the()->delete_unused_inodes (INodeRepo::DM_SOME);

          bfsyncfs_update_read_only(); // check for finished operations
        }
    }
}

bool
Server::decode (const vector<char>& buffer, vector<string>& request)
{
  string client_req_len;
  int    len = -1;
  for (size_t i = 0; i < buffer.size(); i++)
    {
      if (buffer[i] == 0)
        {
          len = atoi (&buffer[0]);
          break;
        }
    }
  if (len == -1)
    return false;

  size_t skip = strlen (&buffer[0]) + 1; // skip len field
  if (buffer.size() != skip + len)
    return false;

  request.clear();
  string s;
  for (size_t i = skip; i < buffer.size(); i++)
    {
      if (buffer[i] == 0)
        {
          request.push_back (s);
          s = "";
        }
      else
        {
          s += buffer[i];
        }
    }
  return true;
}

void
Server::encode (const vector<string>& reply, vector<char>& buffer)
{
  buffer.clear();

  // compute reply size
  size_t reply_size = 0;
  for (size_t i = 0; i < reply.size(); i++)
    reply_size += reply[i].size() + 1;

  // build buffer
  string l = string_printf ("%zd", reply_size);

  buffer.resize (l.size() + 1 + reply_size);

  // len => buffer
  vector<char>::iterator bi = buffer.begin();
  std::copy (l.begin(), l.end(), bi);
  bi += l.size() + 1;

  for (size_t i = 0; i < reply.size(); i++)
    {
      // reply[i] => buffer
      std::copy (reply[i].begin(), reply[i].end(), bi);
      bi += reply[i].size() + 1;
    }
}

void
Server::handle_client (int client_fd)
{
  struct pollfd cpoll_fds[1];
  FSLock *lock = 0;

  vector<char> client_req;

  while (client_fd > 0)
    {
      cpoll_fds[0].fd = client_fd;
      cpoll_fds[0].events = POLLIN;
      cpoll_fds[0].revents = 0;
      if (poll (cpoll_fds, 1, -1) > 0)
        {
          char buffer[1024];
          int len = read (client_fd, buffer, 1024);

          if (len == 0 /* remote end closed connection */
          || (len == -1 && errno != EAGAIN && errno != EINTR)) /* some error */
            {
              close (client_fd);
              client_fd = -1;
            }
          else
            {
              client_req.insert (client_req.end(), buffer, buffer + len);
            }
        }
      debug ("Server: client_req.size() = %zd\n", client_req.size());
      vector<string> request;
      if (decode (client_req, request))
        {
          /*
          printf ("REQUEST:\n");
          for (vector<string>::iterator ri = request.begin(); ri != request.end(); ri++)
            {
              printf (" - %s\n", ri->c_str());
            }
          */
          vector<string> result;
          if (request.size() > 0)
            {
              if (request[0] == "print")
                {
                  for (size_t i = 1; i < request.size(); i++)
                    printf ("%s", request[i].c_str());
                  result.push_back ("ok");
                }
              else if (request[0] == "get-lock")
                {
                  if (lock)
                    result.push_back ("fail: lock already acquired");
                  else
                    {
                      lock = new FSLock (FSLock::RDONLY);
                      result.push_back ("ok");

                      FSLock sc_lock (FSLock::REORG);
                      INodeRepo::the()->save_changes();
                    }
                }
              else if (request[0] == "save-changes")
                {
                  if (!lock)
                    result.push_back ("fail: save-changes requires using get-lock first");
                  else
                    {
                      result.push_back ("ok");

                      FSLock sc_lock (FSLock::REORG);
                      INodeRepo::the()->save_changes();
                    }
                }
              else if (request[0] == "get-prof")
                {
                  result.push_back (TimeProf::the()->result());
                }
              else if (request[0] == "reset-prof")
                {
                  TimeProf::the()->reset();

                  result.push_back ("ok");
                }
              else if (request[0] == "clear-cache")
                {
                  if (!lock)
                    result.push_back ("fail: clear-cache requires using get-lock first");
                  else
                    {
                      result.push_back ("ok");

                      FSLock cc_lock (FSLock::REORG);
                      INodeRepo::the()->clear_cache();
                    }
                }
              else if (request[0] == "update-read-only")
                {
                  result.push_back ("ok");
                  bfsyncfs_update_read_only();
                }
              else if (request[0] == "perf-getattr")
                {
                  string filename = request[1];
                  int    count    = atoi (request[2].c_str());

                  struct stat st;
                  double time = gettime();
                  for (int i = 0; i < count; i++)
                    {
                      int rc = bfsync_getattr (filename.c_str(), &st);
                      if (rc != 0)
                        {
                          result.push_back ("fail");
                          break;
                        }
                    }
                  time = gettime() - time;
                  if (!result.size())
                    {
                      string msg = string_printf ("getattr took %.2f ms <=> %.f getattr/s",
                                                  time * 1000, count / time);
                      result.push_back (msg);
                    }
                }
              else if (request[0] == "perf-getattr-list")
                {
                  string filename = request[1];

                  vector<string> names;
                  FILE *f = fopen (filename.c_str(), "r");
                  char buffer[16 * 1024];
                  while (fgets (buffer, 16 * 1024, f))
                    {
                      strtok (buffer, "\n");
                      if (buffer[0] != '#')
                        names.push_back (buffer);
                    }
                  fclose (f);

                  struct stat st;
                  double time = gettime();
                  for (int i = 0; i < names.size(); i++)
                    {
                      int rc = bfsync_getattr (names[i].c_str(), &st);
                      if (rc != 0)
                        {
                          result.push_back ("fail");
                          break;
                        }
                      if ((i % 100000) == 0)
                        INodeRepo::the()->delete_unused_keep_count (100000);
                    }
                  time = gettime() - time;

                  if (!result.size())
                    {
                      string msg = string_printf ("getattr took %.2f ms <=> %.f getattr/s",
                                                  time * 1000, names.size() / time);
                      result.push_back (msg);
                    }
                }
            }
          vector<char> rbuffer;
          encode (result, rbuffer);
          write (client_fd, &rbuffer[0], rbuffer.size());
          client_req.clear();
        }
    }
  bfsyncfs_update_read_only(); // there might be a new operation in journal now
  if (lock)
    {
      delete lock;
      lock = NULL;
    }
  // update history (relevant after commits)
  INodeRepo::the()->bdb->history()->read();
}
