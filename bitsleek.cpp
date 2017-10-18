#include <exception>
#include <chrono>
#include <sstream>
#include <unordered_map>
#include <cwchar>
#include "restAPI.h"
#include "torrentManager.h"
#include "config.h"
#include "lib/plog/Log.h"

int main(int argc, char const* argv[])
{

	ConfigManager config;	
	std::string log_file_path = "./log/bitsleek-log.txt";
	size_t log_max_size = 5*1024*1024; // 5MB
	int log_max_files = 3;	
	plog::Severity log_severity = plog::Severity::debug;
	std::string download_path = "./";
	
	std::unordered_map<std::string, plog::Severity> map_log_severity({{"none",plog::Severity::none},
		       			{"fatal",plog::Severity::fatal},
					{"error",plog::Severity::error},
					{"warning",plog::Severity::warning},
					{"info",plog::Severity::info},
					{"debug",plog::Severity::debug},
					{"verbose",plog::Severity::verbose}});
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
	}
	plog::init(log_severity, log_file_path.c_str(), log_max_size, log_max_files);

	LOG_DEBUG << "Starting Bitsleek";

	TorrentManager torrent_manager;
	
	RestAPI api(config, torrent_manager);
	api.start_server();
	
	try {
		download_path = config.get_config("directory.download_path");
	}
	catch(boost::property_tree::ptree_error &e) {
		std::cerr << e.what() << std::endl;
	}
	torrent_manager.add_torrent_async("/mnt/DATA/Codacao/bitsleek/test/archlinux-2017.06.01-x86_64.iso.torrent", download_path);
	torrent_manager.add_torrent_async("/mnt/DATA/Codacao/bitsleek/test/debian-9.1.0-amd64-i386-netinst.iso.torrent", download_path);
	torrent_manager.add_torrent_async("/mnt/DATA/Codacao/bitsleek/test/debian-9.1.0-amd64-netinst.iso.torrent", download_path);
	torrent_manager.add_torrent_async("/mnt/DATA/Codacao/bitsleek/test/debian-9.1.0-arm64-netinst.iso.torrent", download_path);
	torrent_manager.add_torrent_async("/mnt/DATA/Codacao/bitsleek/test/debian-9.1.0-armel-netinst.iso.torrent", download_path);
	torrent_manager.add_torrent_async("/mnt/DATA/Codacao/bitsleek/test/debian-9.1.0-armhf-netinst.iso.torrent", download_path);
	torrent_manager.add_torrent_async("/mnt/DATA/Codacao/bitsleek/test/debian-9.1.0-i386-netinst.iso.torrent", download_path);
	torrent_manager.add_torrent_async("/mnt/DATA/Codacao/bitsleek/test/debian-9.1.0-mips64el-netinst.iso.torrent", download_path);
	torrent_manager.add_torrent_async("/mnt/DATA/Codacao/bitsleek/test/debian-9.1.0-mipsel-netinst.iso.torrent", download_path);
	torrent_manager.add_torrent_async("/mnt/DATA/Codacao/bitsleek/test/debian-9.1.0-mips-netinst.iso.torrent", download_path);
	torrent_manager.add_torrent_async("/mnt/DATA/Codacao/bitsleek/test/debian-9.1.0-ppc64el-netinst.iso.torrent", download_path);
	torrent_manager.add_torrent_async("/mnt/DATA/Codacao/bitsleek/test/debian-9.1.0-s390x-netinst.iso.torrent", download_path);
	torrent_manager.add_torrent_async("/mnt/DATA/Codacao/bitsleek/test/debian-live-9.0.1-amd64-lxde.iso.torrent", download_path);
	torrent_manager.add_torrent_async("/mnt/DATA/Codacao/bitsleek/test/kali-linux-2017.1-amd64.torrent", download_path);	
	
	
	while(true) {
		torrent_manager.update_torrent_console_view();
		torrent_manager.check_alerts();
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
	}

	LOG_INFO << "Closing Bitsleek";

	try {
		LOG_DEBUG << "Saving config";
		config.save_config(); }
	catch(boost::property_tree::ptree_error &e) { 
		std::cerr << e.what() << std::endl;
		LOG_ERROR << e.what();
	}
	
	LOG_DEBUG << "Stopping API";
	api.stop_server();

	return 0;
}
