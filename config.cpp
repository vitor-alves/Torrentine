#include "config.h"
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <iostream>
#include <cstddef>

void ConfigManager::save_config() {
	boost::property_tree::ini_parser::write_ini("config/config.ini", config_pt);
}

void ConfigManager::load_config() {
	boost::property_tree::ini_parser::read_ini("config/config.ini", config_pt);
}

const std::string ConfigManager::get_config(std::string config_name) {
	return config_pt.get<std::string>(config_name);
}

void ConfigManager::set_config(std::string name, std::string value) {
	config_pt.put(name, value);
}

ConfigManager::ConfigManager() {
	ConfigManager::load_config();
}

ConfigManager::~ConfigManager() {

}
