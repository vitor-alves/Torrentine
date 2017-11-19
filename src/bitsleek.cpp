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
	catch(std::exception const &e) {
		std::cerr << "Problem parsing arguments: " << e.what() << std::endl;
		return 1;
	}

	ConfigManager config(config_file);
	
	initialize_log(config);
	
	TorrentManager torrent_manager;
	
	add_test_torrents(torrent_manager, config);

	RestAPI api(config, torrent_manager);
	api.start_server();

	signal(SIGINT, shutdown_program);	
	while(!shutdown_flag) {
		torrent_manager.update_torrent_console_view();
		torrent_manager.check_alerts();
		torrent_manager.wait_for_alert(lt::milliseconds(1000));	
	}
	
	config.save_config(config_file);
	
	api.stop_server();
	
	return 0;
}

void shutdown_program(int s) {
	shutdown_flag = 1;	
}

void parse_arguments(int argc, char const* argv[], fs::path &config_file) {
	po::options_description description("Bitsleek Usage");
	description.add_options()
		("help,h", "Display this help message")
		("version,v", "Display version information")
		("config,c", po::value<fs::path>(&config_file)->value_name("file_path")->default_value("config/config.ini"), "Specify the configuration file path");
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


void initialize_log(ConfigManager &config) {		
	std::string log_file_path; // TODO should be fs::path
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
		plog::init(log_severity, log_file_path.c_str(), log_max_size, log_max_files);
		LOG_DEBUG << "Log initialized";
	}
	catch(const boost::property_tree::ptree_error &e) {
		std::cerr << e.what() << std::endl;
	       	LOG_ERROR << "Could not get config: " << e.what();
		return;
	}
}

void add_test_torrents(TorrentManager &torrent_manager, ConfigManager &config) {	
	std::string download_path = "./";

	try {
		download_path = config.get_config("directory.download_path");
		LOG_DEBUG << "Added test torrents";
	}
	catch(const boost::property_tree::ptree_error &e) {
		std::cerr << e.what() << std::endl;
		LOG_ERROR << "Could not get config: " << e.what();
		return;
	}
	bool success_t1 = torrent_manager.add_torrent_async("/mnt/DATA/Codacao/bitsleek/test/sample_torrents/debian-9.1.0-amd64-i386-netinst.iso.torrent", download_path);
	bool success_t2 = torrent_manager.add_torrent_async("/mnt/DATA/Codacao/bitsleek/test/sample_torrents/debian-9.1.0-amd64-netinst.iso.torrent", download_path);
}
