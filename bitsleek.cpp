#include <exception>
#include <chrono>
#include "restAPI.h"
#include "torrentManager.h"
#include "config.h"

int main(int argc, char const* argv[])
{	
	/* Load config */
	ConfigManager config;	
	std::string download_path;
	int api_port;
	try {
		download_path = config.get_config("directory.download_path");
		}
	catch(boost::property_tree::ptree_error &e) {
		std::cerr << e.what() << std::endl; } // TODO - log this

	/* Create TorrentManager */
	TorrentManager torrent_manager;
	
	/* Start API */
	RestAPI api(config, torrent_manager);
	api.start_server();

	/* Add a few torrents for test */
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

	/* Save config */
	try {
		config.save_config(); }
	catch(boost::property_tree::ptree_error &e) { 
		std::cerr << e.what() << std::endl; } // TODO - log this	
	
	/* Stop API server */	
	api.stop_server();

	return 0;
}
