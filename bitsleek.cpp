#include <exception>
#include <chrono>
#include <sstream>
#include <unordered_map>
#include <cwchar>
#include "restAPI.h"
#include "bitsleek.h"

int main(int argc, char const* argv[])
{	
	ConfigManager config;
	TorrentManager torrent_manager;
	
	initialize_log(config);
	
	LOG_INFO << "Starting Bitsleek";

	add_test_torrents(torrent_manager, config);

	RestAPI api(config, torrent_manager);
	api.start_server();
	
	while(true) {
		torrent_manager.update_torrent_console_view();
		torrent_manager.check_alerts();
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
	}

	try {
		config.save_config();	
		LOG_DEBUG << "Config Saved";
	}
	catch(boost::property_tree::ptree_error &e) { 
		std::cerr << e.what() << std::endl;
		LOG_ERROR << e.what();
	}
	
	LOG_DEBUG << "Stopping API";
	api.stop_server();

	return 0;
}


void initialize_log(ConfigManager &config) {		
	std::string log_file_path = "./log/bitsleek-log.txt";
	size_t log_max_size = 5*1024*1024; // 5MB
	int log_max_files = 3;	
	plog::Severity log_severity = plog::Severity::debug;
	
	try {
		std::stringstream sstream;
		log_file_path = config.get_config("log.file_path");
		sstream = std::stringstream(config.get_config("log.max_size"));
		sstream >> log_max_size;
		sstream = std::stringstream(config.get_config("log.max_files"));
		sstream >> log_max_files;
		std::unordered_map<std::string, plog::Severity>::iterator it_log_severity = map_log_severity.find(config.get_config("log.severity"));
		if(it_log_severity != map_log_severity.end())
			log_severity = it_log_severity->second;
	}
	catch(const boost::property_tree::ptree_error &e) {
		std::cerr << e.what() << std::endl;
	       	LOG_ERROR << "Could not get config: " << e.what();
		return;
	}
	plog::init(log_severity, log_file_path.c_str(), log_max_size, log_max_files);
	LOG_DEBUG << "Log initialized";
}

void add_test_torrents(TorrentManager &torrent_manager, ConfigManager &config) {	
	std::string download_path = "./";

	try {
		download_path = config.get_config("directory.download_path");
		LOG_DEBUG << "Added test torrents";
	}
	catch(boost::property_tree::ptree_error &e) {
		std::cerr << e.what() << std::endl;
		LOG_ERROR << "Could not get config: " << e.what();
		return;
	}
	bool success_t1 = torrent_manager.add_torrent_async("./test/debian-9.1.0-amd64-i386-netinst.iso.torrent", download_path);
	bool success_t2 = torrent_manager.add_torrent_async("./test/debian-9.1.0-amd64-netinst.iso.torrent", download_path);
}
