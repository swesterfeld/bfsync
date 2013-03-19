// Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

#include <string>
#include <vector>

namespace BFSync
{

class Server
{
  bool        socket_ok;
  int         socket_fd;
  std::string socket_path;
  int         wakeup_pipe_fds[2];
  pthread_t   thread;
  bool        thread_running;

public:
  Server();
  ~Server();

  bool init_socket (const std::string& repo_path);
  void start_thread();
  void stop_thread();

  // server thread:
  void run();
  void handle_client (int client_fd);
  bool decode (const std::vector<char>& buffer, std::vector<std::string>& request);
  void encode (const std::vector<std::string>& result, std::vector<char>& buffer);
};

}
