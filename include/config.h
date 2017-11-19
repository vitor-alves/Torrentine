#include <string>
#include <boost/filesystem.hpp>
#include "cpptoml/cpptoml.h"
#include <exception>

#ifndef CONFIG_H
#define CONFIG_H
namespace fs = boost::filesystem;

class config_key_error: public std::exception {
public:
	std::string message;
	config_key_error(std::string message) : message(message){ };
	virtual const char* what() const throw() {
		return message.c_str();
	};
};

// TODO - make this singleton
class ConfigManager {
private:
	std::shared_ptr<cpptoml::table> config_toml;
public:
	void save_config(fs::path const config_file);
	void load_config(fs::path const config_file);
	void set_config(std::string name, std::string value);
	ConfigManager();
	~ConfigManager();

public:
	template <class T>
	const T get_config(std::string const key) {
		auto value = config_toml->get_qualified_as<std::string>(key); // TODO change std::string to T. Adjust callers and config.toml

		if(value) {
			return *value;
		}
		else {
			throw config_key_error("config key not found");
		}
	}
	// This assumes the path/key are valid and exist in the config file
	template <class T>
	void set_config(std::string const path, std::string const key, T const value) {
		auto table = config_toml->get_table_qualified(path);
		table->insert(key, value);
	}
};
#endif
