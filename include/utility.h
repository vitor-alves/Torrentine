#include "torrent.h"
#include <vector>

std::string random_string(std::string chars, int const size);
bool file_to_buffer(std::vector<char> &buffer, std::string const filename);
std::vector<unsigned long int> split_string_to_ulong(std::string const &str, const char delim);
bool str_to_bool(std::string s);

