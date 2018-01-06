#include <string>
#include <boost/filesystem.hpp>
#include "cpptoml/cpptoml.h"
#include <exception>

#ifndef CONFIG_H
#define CONFIG_H

namespace fs = boost::filesystem;

class config_key_error: public std::exception {
private:
	std::string message;
public:
	config_key_error(std::string message) : message(message){ };
	virtual const char* what() const throw() {
		return message.c_str();
	};
};

class ConfigManager {
private:
	std::shared_ptr<cpptoml::table> config_toml;
	bool create_default_config_file(fs::path const config_file);
public:
	void save_config(fs::path const config_file);
	void load_config(fs::path const config_file);

public:
	template <class T>
	const T get_config(std::string const key) {
		auto value = config_toml->get_qualified_as<T>(key); 

		if(value) {
			return *value;
		}
		else {
			throw config_key_error("config key not found");
		}
	}
	// This assumes the path/key are valid and exist in the config file
	// TODO - this needs testing. I am not using this yet
	// TODO - setting config is not so simple. Changing things like download path may cause troubles. Maybe a full restart is needed to apply some settings because if they were changed while program is running problems would occur.
	// Find a good way to deal with this.
	template <class T>
	void set_config(std::string const path, std::string const key, T const value) {
		auto table = config_toml->get_table_qualified(path);
		// TODO - remember about exceptions config_key_error(message) here. 
		table->insert(key, value);
	}
};
#endif
