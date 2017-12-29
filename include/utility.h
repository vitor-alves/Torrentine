#include "torrent.h"
#include <vector>
#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;

std::string random_string(std::string chars, int const size);
bool file_to_buffer(std::vector<char> &buffer, std::string const filename);
std::vector<unsigned long int> split_string_to_ulong(std::string const &str, const char delim);
bool str_to_bool(std::string s);
bool get_files_in_folder(fs::path const root, std::string const extension, std::vector <fs::path> &filepaths);
std::vector<std::string> split_string(std::string const &s, char const delim);
std::string generate_password_hash(const char* pass, const unsigned char* salt);
std::string gzip_encode(std::string s);
