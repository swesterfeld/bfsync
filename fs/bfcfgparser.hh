// Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

#ifndef BFSYNC_CFG_PARSER_HH
#define BFSYNC_CFG_PARSER_HH

#include <string>
#include <map>
#include <vector>

namespace BFSync
{

enum Token
{
  TOKEN_ERROR = 0,
  TOKEN_IDENTIFIER = 1,
  TOKEN_SEMICOLON,
  TOKEN_STRING,
  TOKEN_OPEN,
  TOKEN_CLOSE,
  TOKEN_EOF
};

class CfgParser
{
  FILE       *cfg_file;
  std::string token_value;
  int         line;
  std::string filename;
  std::string m_error;

  std::map<std::string, std::vector<std::string> > m_values;

  Token get_token();
  void  print_token (Token t);

  bool parse_group_or_key_value (std::string name);
  bool parse_group (std::string name);
  bool parse_error (const std::string& error);
public:
  bool parse (const std::string& filename);
  const std::map<std::string, std::vector<std::string> >& values();
  const std::string& error();
};

}

#endif
