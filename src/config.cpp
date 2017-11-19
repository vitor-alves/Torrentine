#include "config.h"
#include <iostream>
#include <cstddef>
#include "plog/Log.h"
#include <fstream>

void ConfigManager::save_config(fs::path const config_file) {
		
	std::ofstream out_config_file(config_file.string());
	if(out_config_file.is_open()) {
		out_config_file << *config_toml;
		out_config_file.close();
		LOG_DEBUG << "Config Saved";
	}
	else {
		LOG_ERROR << "Could not open file: " << config_file.string();
	}
}

void ConfigManager::load_config(fs::path const config_file) {
	// TODO - check if config exists and is in valid format. If not valid, create one that is valid and rename the invalid.
	// If it doesnt exist create the file somewhere. Remember there are fields that NEED to exist, like the log path and other stuff

	config_toml = cpptoml::parse_file(config_file.string());
}

ConfigManager::ConfigManager() {

}

ConfigManager::~ConfigManager() {

}
