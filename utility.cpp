#include <random>
#include <string>
#include <sstream>
#include "utility.h"

/* Generate random string */
std::string randomString(std::string chars, int size) {
	std::random_device rgn;
	std::uniform_int_distribution<> index_dist(0, (chars.size()-1));
	std::stringstream ss;
	for(int i=0; i < size; i++) {
		ss << chars[index_dist(rgn)];
	}
	return ss.str();
}

