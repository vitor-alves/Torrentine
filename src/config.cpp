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
		LOG_ERROR << "Could not open save config file: " << config_file.string();
	}
}

// All messages are send to std::out or std::err because the log is not
// initialized before config gets loaded
void ConfigManager::load_config(fs::path const config_file) {
	if(!fs::exists(config_file)) {
		std::cerr << "Could not find config file: " << config_file.string() <<
		       	". An attempt to create a new default config file will be made" << std::endl;
		create_default_config_file(config_file);
	}

	config_toml = cpptoml::parse_file(config_file.string());
}

bool ConfigManager::create_default_config_file(fs::path const config_file) {
	std::ofstream out_config_file;
	out_config_file.open(config_file.string());
	if(out_config_file.is_open()) {
		std::shared_ptr<cpptoml::table> root = cpptoml::make_table();
		
		std::shared_ptr<cpptoml::table> table_directory = cpptoml::make_table();
		table_directory->insert("torrent_file_path", "./temp/");
		table_directory->insert("download_path", "./");
		root->insert("directory", table_directory);
		
		std::shared_ptr<cpptoml::table> table_log = cpptoml::make_table();
		table_log->insert("severity", "info");
		table_log->insert("file_path", "log/bitsleek-log.txt");
		table_log->insert("max_size", 5242880);
		root->insert("log", table_log);
				
		std::shared_ptr<cpptoml::table> table_api = cpptoml::make_table();
		table_api->insert("port", 8040);
		table_api->insert("address", "0.0.0.0");
		root->insert("api", table_api);
		
		out_config_file << *root;
		out_config_file.close();	
		std::cout << "Created new default config file: " << config_file.string() << std::endl;
		return true;
	}
	else {
		std::cerr << "Could not create a new default config file: " << config_file.string() << std::endl;
		return false;
	}
}
