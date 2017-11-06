#include <random>
#include <string>
#include <sstream>
#include <fstream>
#include <string>
#include "utility.h"
#include "lib/plog/Log.h"

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

// Splits integer only string separated by 'delim'
std::vector<unsigned long int> split_string_ulong(const std::string &str, char delim) {
	std::vector<unsigned long int> ids;
	std::stringstream ss(str);

	std::string item;
	while(std::getline(ss, item, ',')) {
		if(!item.empty()) {
			ids.push_back(std::stoul(item));
		}
	}
	return ids;
}
