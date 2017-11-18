#include <string>
#include <boost/property_tree/ptree.hpp>
#include <boost/filesystem.hpp>

#ifndef CONFIG_H
#define CONFIG_H
namespace fs = boost::filesystem;

// TODO - make this singleton
class ConfigManager {
private:
	boost::property_tree::ptree config_pt;
public:
	void save_config(fs::path const config_file);
	void load_config(fs::path const config_file);
	const std::string get_config(std::string name); // TODO - use templates to return different types instead of strings only
	void set_config(std::string name, std::string value);
	ConfigManager(fs::path const config_file);
	~ConfigManager();
};

#endif
