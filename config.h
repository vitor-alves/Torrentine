#include <string>
#include <boost/property_tree/ptree.hpp>

#ifndef CONFIG_H
#define CONFIG_H

// TODO - make this singleton
class ConfigManager {
private:
	boost::property_tree::ptree config_pt;
public:
	void save_config();
	void load_config();
	const std::string get_config(std::string name);
	void set_config(std::string name, std::string value);
	ConfigManager();
	~ConfigManager();
};

#endif
