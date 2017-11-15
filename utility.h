#include "torrent.h"
#include <vector>

std::string random_string(std::string chars, int size);
bool file_to_buffer(std::vector<char> &buffer, const std::string filename);
std::vector<unsigned long int> split_string_to_ulong(const std::string &str, const char delim);
bool validade_authorization(std::string authorization_base64);
