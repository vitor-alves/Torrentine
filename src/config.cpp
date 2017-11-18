#include "config.h"
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <iostream>
#include <cstddef>
#include "plog/Log.h"

void ConfigManager::save_config(fs::path const config_file) {
	try {
		boost::property_tree::ini_parser::write_ini(config_file.string(), config_pt);
		LOG_DEBUG << "Config Saved";
	}
	catch(const boost::property_tree::ptree_error &e) { 
		LOG_ERROR << e.what();
	}
}

void ConfigManager::load_config(fs::path const config_file) {
	// TODO - check if config exists and is in valid format. If not valid, create one that is valid and rename de invalid.
	// If it doesnt exist create the file somewhere. Remember there are fields that NEED to exist, like the log path and other stuff
	boost::property_tree::ini_parser::read_ini(config_file.string(), config_pt);
}

const std::string ConfigManager::get_config(std::string config_name) {
	return config_pt.get<std::string>(config_name);
}

void ConfigManager::set_config(std::string name, std::string value) {
	config_pt.put(name, value);
}

ConfigManager::ConfigManager(fs::path const config_file) {
	ConfigManager::load_config(config_file);
}

ConfigManager::~ConfigManager() {

}
