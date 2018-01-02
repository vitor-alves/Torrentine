#include <random>
#include <string>
#include <sstream>
#include <fstream>
#include <string>
#include <cctype>
#include "utility.h"
#include <cstring>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/crypto.h>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filter/gzip.hpp>

std::string random_string(std::string chars, int const size) {
	std::random_device rgn;
	std::uniform_int_distribution<> index_dist(0, (chars.size()-1));
	std::stringstream ss;
	for(int i=0; i < size; i++) {
		ss << chars[index_dist(rgn)];
	}
	return ss.str();
}

bool file_to_buffer(std::vector<char> &buffer, std::string const filename) {
	std::ifstream ifs(filename);	
	if (ifs.eof() || ifs.fail()) {
		return false;
	}
	else {	
		ifs.seekg(0, std::ios_base::end);
		std::streampos fileSize = ifs.tellg();
		buffer.resize(fileSize);
		ifs.seekg(0, std::ios_base::beg);
		ifs.read(&buffer[0], fileSize);
		return true;
	}
}

// Splits string of integers saparated by char 'delim'. Assumes string is in valid format.
std::vector<unsigned long int> split_string_to_ulong(std::string const &str, const char delim) {
	std::vector<unsigned long int> numbers;
	std::stringstream ss(str);

	std::string item;
	while(std::getline(ss, item, ',')) {
		if(!item.empty()) {
			numbers.push_back(std::stoul(item));
		}
	}
	return numbers;
}

bool str_to_bool(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(), ::tolower);
	bool b;
	std::istringstream(s) >> std::boolalpha >> b;
	return b;
}

bool get_files_in_folder(const fs::path root, const std::string extension, std::vector <fs::path> &filepaths) {
	if(!fs::exists(root) || !fs::is_directory(root)) {
		return false;
	}	

	fs::recursive_directory_iterator it(root);
	fs::recursive_directory_iterator end_it;

	while(it != end_it) {
		if(fs::is_regular_file(*it) && it->path().extension() == extension) {
			filepaths.push_back(it->path());
		}
		it++;
	}
	return true;
}

std::vector<std::string> split_string(std::string const &s, char const delim) {
	std::vector<std::string> splitted_strings;
	std::stringstream ss(s);
	std::string temp;
	while(getline(ss, temp, delim)) {
		splitted_strings.push_back(temp);
	}
	return splitted_strings;
}

// Source:
// https://stackoverflow.com/a/22795472/4982631
void PBKDF2_HMAC_SHA_512_string(const char* pass, const unsigned char* salt, int32_t iterations, uint32_t outputBytes, char* hexResult) {
	unsigned int i;
	unsigned char digest[outputBytes];
	PKCS5_PBKDF2_HMAC(pass, strlen(pass), salt, strlen((char*)salt), iterations, EVP_sha512(), outputBytes, digest);
	for (i = 0; i < sizeof(digest); i++)
		sprintf(hexResult + (i * 2), "%02x", 255 & digest[i]);
}

std::string generate_password_hash(const char* pass, const unsigned char* salt) {
	int32_t iterations = 1024; // Low, but acceptable. Kept like this to reduce calculation speed.
	uint32_t outputBytes = 32;
	uint32_t hexResult_size = 2*outputBytes+1; // 2*outputBytes+1 is 2 hex bytes per binary byte and one character at the end for the string-terminating \0
	char hexResult[hexResult_size];
	PBKDF2_HMAC_SHA_512_string(pass, salt, iterations, outputBytes, hexResult);
	
	return std::string(hexResult, hexResult_size-1); // -1 to ignore '\0'. We dont need it in std::strings
}

std::string gzip_encode(std::string s) {
	std::stringstream ss(s), ss_compressed;
	boost::iostreams::filtering_streambuf< boost::iostreams::input> in;
	in.push(boost::iostreams::gzip_compressor());
	in.push(ss);
	boost::iostreams::copy(in, ss_compressed);
	return ss_compressed.str();
}

bool is_text_boolean(std::string const s) {
	if(s == "true" || s == "false") {
		return true;
	}
	return false;
}

bool is_text_int_number(std::string const s) {
	try {
		long num = std::stoi(s);
		return true;
	}
	catch(std::exception const &e) {
		return false;	
	}
}

bool is_text_double_number(std::string const s) {
	try {
		double num = std::stod(s);
		return true;
	}
	catch(std::exception const &e) {
		return false;
	}
}

