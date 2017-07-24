#include <iostream>
#include <thread>
#include <chrono>
#include <fstream>
#include <typeinfo>
#include <linux/rtnetlink.h>
#include <curses.h>
#include <unistd.h>
#include "torrentManager.h"

// TODO - treat errors on error_code, finding file etc
void TorrentManager::add_torrent_async(std::string filename, std::string save_path) {
	/* ADD TORRENT - .TORRENT FILE - ASYNC */
	std::ifstream ifs(filename);
	std::vector<char> buffer;
	if (!ifs.eof() && !ifs.fail())
	{
		ifs.seekg(0, std::ios_base::end);
		std::streampos fileSize = ifs.tellg();
		buffer.resize(fileSize);

		ifs.seekg(0, std::ios_base::beg);
		ifs.read(&buffer[0], fileSize);
	}
	char const *buf = buffer.data();
	lt::bdecode_node node;
	lt::error_code ec;
	int ret =  lt::bdecode(buf, buf+buffer.size(), node, ec);
	lt::torrent_info info(node);
	lt::add_torrent_params atp;
	boost::shared_ptr<lt::torrent_info> t_info = boost::make_shared<lt::torrent_info>(info);
	atp.ti = t_info;
	atp.save_path = save_path;
	session.async_add_torrent(atp);
}

void TorrentManager::update_torrent_console_view() {
	for(lt::torrent_handle handle : torrent_handle_vector) {
		std::cout << "name: " << handle.status().name << " / ";
		std::cout << "down: " << handle.status().download_rate / 1000 << "Kb/s" << " / ";
		std::cout << "up: " << handle.status().upload_rate / 1000 << "Kb/s" << " / ";
		std::cout << "prog: " << handle.status().progress * 100.0 << "%" << " / ";
		//std::cout << "Tracker: " << handle.status().current_tracker << " / ";
		std::cout << "t_down: " << handle.status().total_download / 1000 << "Kb" << " / ";
		std::cout << "t_up: " << handle.status().total_upload / 1000 << "Kb" << " / ";
		std::cout << "seeds: " << handle.status().num_seeds<< " / ";
		std::cout << "peers: " << handle.status().num_peers << " / " << std::endl;
	}
	std::cout << std::endl;
}

void TorrentManager::check_alerts() {
	std::vector<lt::alert*> alerts;
	session.pop_alerts(&alerts);

	for (lt::alert const* a : alerts) {
		//std::cout << a->message() << "\n" << std::endl;
		// if we receive the finished alert or an error, we're done
		if (lt::alert_cast<lt::torrent_finished_alert>(a)) {
			// TODO - goto done;
		}
		if (lt::alert_cast<lt::torrent_error_alert>(a)) {
			std::cout << "ALERT ERROR" << std::endl;
			exit(1);
		}
		if (lt::alert_cast<lt::add_torrent_alert>(a)) {
			lt::add_torrent_alert const * a_temp = lt::alert_cast<lt::add_torrent_alert>(a);
			torrent_handle_vector.push_back(a_temp->handle);
		}
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

std::vector<lt::torrent_handle>& TorrentManager::get_torrent_handle_vector() {
	return torrent_handle_vector;
}

TorrentManager::TorrentManager() {

}

TorrentManager::~TorrentManager() {
    // TODO destruct torrent handles. "all torrent_handles must be destructed before the session is destructed!"
    session.~session(); // TODO - make this async
}

