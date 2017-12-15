#include <exception>
#include <chrono>
#include <sstream>
#include <unordered_map>
#include <cwchar>
#include "restAPI.h"
#include "bitsleek.h"
#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <cstdlib>

namespace po = boost::program_options;
namespace fs = boost::filesystem;

volatile sig_atomic_t shutdown_flag = 0;
int main(int argc, char const* argv[])
{
	fs::path config_file;
	try {
		parse_arguments(argc, argv, config_file);
	}
	catch(po::error const &e) {
		std::cerr << "Problem parsing arguments: " << e.what() << std::endl;
		return 1;
	}
	
	ConfigManager config;
	try {
		config.load_config(config_file);
	}
	catch(cpptoml::parse_exception const &e) {
		// Try appending ".old" to config file so next time program starts it creates a new default config file
		std::stringstream ss;
		ss << config_file.string() << ".old";
		bool result = std::rename(config_file.string().c_str(), ss.str().c_str()); 
		std::cerr << "Problem with configuration file " << config_file.string() << ". " << e.what() <<
			". This usually happens if the configuration file is in an invalid state or \
		       	when your user has no read/write permissions. Deleting this file may fix the \
			issue next time you run the program." << std::endl;
		return 2;
	}

	initialize_log(config);
	
	// TODO - torrentManager should have a reference to config in its constructor and I should remove all config from method calls	
	TorrentManager torrent_manager;
	torrent_manager.load_session_state(config);
	torrent_manager.load_fastresume(config);
	add_test_torrents(torrent_manager, config);
	

	RestAPI api(config, torrent_manager);
	api.start_server();

	std::chrono::steady_clock::time_point last_save_fastresume = std::chrono::steady_clock::now();
	signal(SIGINT, shutdown_program);
	while(!shutdown_flag) {
		torrent_manager.update_torrent_console_view();
		torrent_manager.check_alerts();
		torrent_manager.wait_for_alert(lt::milliseconds(1000));	
		// TODO - Ideally saving fastresume periodically should be done outside the main thread because this may take some time, but there
		// are some special cases that need to be addressed before putting this in another thread. Libtorrent says:
		// Make sure to not remove_torrent() before you receive the save_resume_data_alert though
		if(std::chrono::steady_clock::now() - last_save_fastresume > std::chrono::seconds(5)) {
			torrent_manager.save_fastresume(config, lt::torrent_handle::save_resume_flags_t::save_info_dict |
							lt::torrent_handle::save_resume_flags_t::only_if_modified);
			last_save_fastresume = std::chrono::steady_clock::now();
		} 
	}

	torrent_manager.pause_session(); // Session is paused so fastresume data will definitely be valid once it finishes
	torrent_manager.save_fastresume(config, lt::torrent_handle::save_resume_flags_t::flush_disk_cache  |
					lt::torrent_handle::save_resume_flags_t::save_info_dict    |
					lt::torrent_handle::save_resume_flags_t::only_if_modified);
	torrent_manager.save_session_state(config);

	config.save_config(config_file);
	
	api.stop_server();
	
	return 0;
}

void shutdown_program(int s) {
	LOG_INFO << "Shutting down program";
	shutdown_flag = 1;	
}

void parse_arguments(int const argc, char const* argv[], fs::path &config_file) {
	po::options_description description("Bitsleek Usage");
	description.add_options()
		("help,h", "Display this help message")
		("version,v", "Display version information")
		("config,c", po::value<fs::path>(&config_file)->value_name("file_path")->default_value("config/config.toml"), "Specify the configuration file path");
	po::variables_map vmap;
	po::store(po::command_line_parser(argc, argv).options(description).run(), vmap);
	po::notify(vmap);
	if(vmap.count("help")) {
		std::cout << description << std::endl;
		exit(1);	
	}
	if(vmap.count("version")) {
		std::cout << bitsleek_version << std::endl;
		exit(1);	
	}
}


bool initialize_log(ConfigManager &config) {		
	fs::path log_file_path = "./";
	size_t log_max_size = 5*1024*1024; // 5MB
	int log_max_files = 3;	
	plog::Severity log_severity = plog::Severity::debug;
		
	try {
		std::stringstream sstream;
		log_file_path = fs::path(config.get_config<std::string>("log.file_path"));
		log_max_size = config.get_config<size_t>("log.max_size");
		log_max_files = config.get_config<int>("log.max_files");
		std::unordered_map<std::string, plog::Severity>::iterator it_log_severity =
		       	map_log_severity.find(config.get_config<std::string>("log.severity"));
		if(it_log_severity != map_log_severity.end())
			log_severity = it_log_severity->second;
	}
	catch(config_key_error const &e) {
		std::cerr << "The log was not initialized. Could not get config when initializing log: "
		       		<< e.what() << std::endl;
		return false;
	}
	plog::init(log_severity, log_file_path.c_str(), log_max_size, log_max_files);
	LOG_DEBUG << "Log initialized";
	return true;
}

void add_test_torrents(TorrentManager &torrent_manager, ConfigManager &config) {	
	std::string download_path = "./";

	try {
		download_path = config.get_config<std::string>("directory.download_path");
	}
	catch(const config_key_error &e) {
		LOG_ERROR << "Could not get config: " << e.what();
		return;
	}
	bool success_t1 = torrent_manager.add_torrent_async("/mnt/DATA/Codacao/bitsleek/test/sample_torrents/debian-9.1.0-amd64-i386-netinst.iso.torrent", download_path);
	bool success_t2 = torrent_manager.add_torrent_async("/mnt/DATA/Codacao/bitsleek/test/sample_torrents/debian-9.1.0-amd64-netinst.iso.torrent", download_path);
	LOG_DEBUG << "Added test torrents";
}
