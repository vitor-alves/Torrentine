#include <exception>
#include <chrono>
#include <sstream>
#include <unordered_map>
#include <cwchar>
#include "restAPI.h"
#include "torrentine.h"
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
	
	TorrentManager torrent_manager(config);
	//torrent_manager.load_session_settings();
	torrent_manager.load_session_state();
	torrent_manager.load_session_extensions();
	torrent_manager.load_fastresume();

	RestAPI api(config, torrent_manager);
	api.start_server();

	std::chrono::steady_clock::time_point last_save_fastresume = std::chrono::steady_clock::now();
	std::chrono::steady_clock::time_point last_post_session_stats = std::chrono::steady_clock::now();
	signal(SIGINT, shutdown_program);
	while(!shutdown_flag) {
		torrent_manager.update_torrent_console_view();
		torrent_manager.check_alerts();
		if(std::chrono::steady_clock::now() - last_post_session_stats > std::chrono::seconds(2)) {
			torrent_manager.post_session_stats();
			last_post_session_stats  = std::chrono::steady_clock::now();
		} 
		torrent_manager.wait_for_alert(lt::milliseconds(1000));	
		// TODO - Ideally saving fastresume periodically should be done outside the main thread because this may take some time, but there
		// are some special cases that need to be addressed before putting this in another thread. Libtorrent says:
		// Make sure to not remove_torrent() before you receive the save_resume_data_alert though. What happens is that the removed torrent may 
		// incorrectly still exist in the fastresume data.
		if(std::chrono::steady_clock::now() - last_save_fastresume > std::chrono::seconds(60)) {
			torrent_manager.save_fastresume(lt::torrent_handle::save_resume_flags_t::save_info_dict |
							lt::torrent_handle::save_resume_flags_t::only_if_modified);
			last_save_fastresume = std::chrono::steady_clock::now();
		} 
	}

	torrent_manager.pause_session(); // Session is paused so fastresume data will be valid once it finishes
	torrent_manager.save_fastresume(lt::torrent_handle::save_resume_flags_t::flush_disk_cache  |
					lt::torrent_handle::save_resume_flags_t::save_info_dict            |
					lt::torrent_handle::save_resume_flags_t::only_if_modified);
	torrent_manager.save_session_state();

	config.save_config(config_file);
	
	api.stop_server();
	
	return 0;
}

void shutdown_program(int s) {
	LOG_INFO << "Shutting down program";
	shutdown_flag = 1;	
}

void parse_arguments(int const argc, char const* argv[], fs::path &config_file) {
	po::options_description description("Torrentine Usage");
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
		std::cout << torrentine_version << std::endl;
		exit(1);	
	}
}


bool initialize_log(ConfigManager &config) {		
	fs::path log_file_path = "./";
	size_t log_max_size = 5*1024; // 5MB
	int log_max_files = 1; // Currently only 1 log file. Changing this will require changes in the API to get logs.
				// This adds unnecessary complexity.
	plog::Severity log_severity = plog::Severity::debug;
		
	try {
		std::stringstream sstream;
		log_file_path = fs::path(config.get_config<std::string>("log.file_path"));
		log_max_size = config.get_config<size_t>("log.max_size");
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
