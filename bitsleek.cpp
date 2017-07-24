#include "restAPI.h"
#include "torrentManager.h"

int main(int argc, char const* argv[])
{
	TorrentManager torrent_manager;

	RestAPI api(9000, torrent_manager);
	api.start_server();

	torrent_manager.add_torrent_async("/mnt/DATA/Codacao/bitsleek/test/archlinux-2017.06.01-x86_64.iso.torrent", "./test/test_download/");
	torrent_manager.add_torrent_async("/mnt/DATA/Codacao/bitsleek/test/debian-9.1.0-amd64-i386-netinst.iso.torrent", "./test/test_download/");
	torrent_manager.add_torrent_async("/mnt/DATA/Codacao/bitsleek/test/debian-9.1.0-amd64-netinst.iso.torrent", "./test/test_download/");
	torrent_manager.add_torrent_async("/mnt/DATA/Codacao/bitsleek/test/debian-9.1.0-arm64-netinst.iso.torrent", "./test/test_download/");
	torrent_manager.add_torrent_async("/mnt/DATA/Codacao/bitsleek/test/debian-9.1.0-armel-netinst.iso.torrent", "./test/test_download/");
	torrent_manager.add_torrent_async("/mnt/DATA/Codacao/bitsleek/test/debian-9.1.0-armhf-netinst.iso.torrent", "./test/test_download/");
	torrent_manager.add_torrent_async("/mnt/DATA/Codacao/bitsleek/test/debian-9.1.0-i386-netinst.iso.torrent", "./test/test_download/");
	torrent_manager.add_torrent_async("/mnt/DATA/Codacao/bitsleek/test/debian-9.1.0-mips64el-netinst.iso.torrent", "./test/test_download/");
	torrent_manager.add_torrent_async("/mnt/DATA/Codacao/bitsleek/test/debian-9.1.0-mipsel-netinst.iso.torrent", "./test/test_download/");
	torrent_manager.add_torrent_async("/mnt/DATA/Codacao/bitsleek/test/debian-9.1.0-mips-netinst.iso.torrent", "./test/test_download/");
	torrent_manager.add_torrent_async("/mnt/DATA/Codacao/bitsleek/test/debian-9.1.0-ppc64el-netinst.iso.torrent", "./test/test_download/");
	torrent_manager.add_torrent_async("/mnt/DATA/Codacao/bitsleek/test/debian-9.1.0-s390x-netinst.iso.torrent", "./test/test_download/");
	torrent_manager.add_torrent_async("/mnt/DATA/Codacao/bitsleek/test/debian-live-9.0.1-amd64-lxde.iso.torrent", "./test/test_download/");

	while(true) {
		torrent_manager.update_torrent_console_view();
		torrent_manager.check_alerts();
	}

	api.stop_server();
	return 0;
}