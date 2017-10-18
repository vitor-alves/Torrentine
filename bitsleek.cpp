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
	//wchar_t* log_file_path = L"./log/bitsleek-log.txt"; // TODO - use this
	size_t log_max_size = 5*1024*1024; // 5MB
	int log_max_files = 3;
	std::string download_path = ".";
	plog::Severity log_severity = plog::Severity::debug;

	std::unordered_map<std::string, plog::Severity> map_log_severity({{"none",plog::Severity::none},
		       			{"fatal",plog::Severity::fatal},
					{"error",plog::Severity::error},
					{"warning",plog::Severity::warning},
					{"info",plog::Severity::info},
					{"debug",plog::Severity::debug},
					{"verbose",plog::Severity::verbose}
					});
	try {
		std::stringstream sstream;
		//log_file_path = config.get_config("log.file_path");
		sstream = std::stringstream(config.get_config("log.max_size"));
		sstream >> log_max_size;
		sstream = std::stringstream(config.get_config("log.max_files"));
		sstream >> log_max_files;
		if(map_log_severity.find(config.get_config("log.severity")) != map_log_severity.end()) // TODO - test if its working
			log_severity = map_log_severity.find(config.get_config("log.severity"))->second;
	}
	catch(const boost::property_tree::ptree_error &e) {
		std::cerr << e.what() << std::endl; 
	}
	plog::init(log_severity, "log/bitsleek-log.txt", log_max_size, log_max_files);

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

	//log->info("Closing Bitsleek");
	try {
	//	log->info("Saving config");
		config.save_config(); }
	catch(boost::property_tree::ptree_error &e) { 
		std::cerr << e.what() << std::endl;
	//	log->error(e.what());
	}
	
	//log->debug("Stopping API");
	api.stop_server();

	return 0;
}
