#include <random>
#include <string>
#include <sstream>
#include <fstream>
#include <string>
#include <cctype>
#include "utility.h"

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
