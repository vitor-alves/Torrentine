#include <random>
#include <string>
#include <sstream>
#include <fstream>
#include <string>
#include <cctype>
#include "utility.h"

std::string random_string(std::string chars, int size) {
	std::random_device rgn;
	std::uniform_int_distribution<> index_dist(0, (chars.size()-1));
	std::stringstream ss;
	for(int i=0; i < size; i++) {
		ss << chars[index_dist(rgn)];
	}
	return ss.str();
}

bool file_to_buffer(std::vector<char> &buffer, const std::string filename) {
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
std::vector<unsigned long int> split_string_to_ulong(const std::string &str, const char delim) {
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
