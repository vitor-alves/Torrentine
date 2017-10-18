#include <exception>
#include <chrono>
#include <sstream>
#include <unordered_map>
#include "restAPI.h"
#include "torrentManager.h"
#include "config.h"
#include "lib/spdlog/spdlog.h"

int main(int argc, char const* argv[])
{
	ConfigManager config;	
	std::shared_ptr<spdlog::logger> log;
	std::string log_file_path = ".";
	size_t log_max_size = 5*1024*1024; // 5MB
	size_t log_max_files = 3;
	std::string download_path = ".";
	spdlog::level::level_enum log_level = spdlog::level::debug;

	std::unordered_map<std::string, spdlog::level::level_enum> map_log_level({{"trace",spdlog::level::trace},
		       			{"debug",spdlog::level::debug},
					{"info",spdlog::level::info},
					{"warn",spdlog::level::warn},
					{"error",spdlog::level::err},
					{"critical",spdlog::level::critical},
					{"off",spdlog::level::off}
					});
	try {
		std::stringstream sstream;
		log_file_path = config.get_config("log.file_path");
		sstream = std::stringstream(config.get_config("log.max_size"));
		sstream >> log_max_size;
		sstream = std::stringstream(config.get_config("log.max_files"));
		sstream >> log_max_files;
		if(map_log_level.find(config.get_config("log.level")) != map_log_level.end())
			log_level = map_log_level.find(config.get_config("log.level"))->second;
	}
	catch(const boost::property_tree::ptree_error &e) {
		std::cerr << e.what() << std::endl; 
	}
	try {   
		size_t queue_size = 4096; // Queue size must be power of 2
		spdlog::set_async_mode(queue_size, spdlog::async_overflow_policy::block_retry, nullptr, std::chrono::seconds(2));	
		spdlog::set_level(log_level);
		log = spdlog::rotating_logger_mt("Bitsleek", log_file_path, log_max_size, log_max_files);
		log->flush_on(spdlog::level::err);	
	}
	catch (const spdlog::spdlog_ex& ex) {
		std::cerr << "Log initialization failed: " << ex.what() << std::endl;
	}	
	log->info("Starting Bitsleek");

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

	log->info("Closing Bitsleek");
	try {
		log->info("Saving config");
		config.save_config(); }
	catch(boost::property_tree::ptree_error &e) { 
		std::cerr << e.what() << std::endl;
		log->error(e.what());
	}
	
	log->debug("Stopping API");
	api.stop_server();

	log->debug("Dropping all loggers"); 
	spdlog::drop_all();
	
	return 0;
}
