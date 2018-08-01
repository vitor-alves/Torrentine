#include "torrent.h"
#include <vector>
#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;

std::string random_string(int const size, std::string chars="abcdefghijklmnopqrstuvwxyz");
bool file_to_buffer(std::vector<char> &buffer, std::string const filename);
std::vector<unsigned long int> split_string_to_ulong(std::string const &str, const char delim);
bool str_to_bool(std::string s);
bool get_files_in_folder(fs::path const root, std::string const extension, std::vector <fs::path> &filepaths);
std::vector<std::string> split_string(std::string const &s, char const delim);
std::string generate_password_hash(const char* pass, const unsigned char* salt);
std::string gzip_encode(std::string s);
bool is_text_boolean(std::string const s);
bool is_text_int_number(std::string const s);
bool is_text_double_number(std::string const s);
bool download_file(const char* url, const char* file_name);
