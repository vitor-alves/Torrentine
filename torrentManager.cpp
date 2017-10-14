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

	for(Torrent* torrent : torrents) {
		std::cout << "name: " << torrent->get_handle().status().name << " / ";
		std::cout << "hash: " << torrent->get_handle().status().info_hash << " / ";
		std::cout << "down: " << torrent->get_handle().status().download_rate / 1000 << "Kb/s" << " / ";
		std::cout << "up: " << torrent->get_handle().status().upload_rate / 1000 << "Kb/s" << " / ";
		std::cout << "prog: " << torrent->get_handle().status().progress * 100.0 << "%" << " / ";
		//std::cout << "Tracker: " << torrent->get_handle().status().current_tracker << " / ";
		std::cout << "t_down: " << torrent->get_handle().status().total_download / 1000 << "Kb" << " / ";
		std::cout << "t_up: " << torrent->get_handle().status().total_upload / 1000 << "Kb" << " / ";
		std::cout << "seeds: " << torrent->get_handle().status().num_seeds<< " / ";
		std::cout << "peers: " << torrent->get_handle().status().num_peers << " / " << std::endl;
		std::cout << "ID: " << torrent->get_id() << " / " << std::endl;
	}
	
	std::cout << std::endl;
}

// TODO - Put all possible alerts here. 
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
			
			Torrent* torrent = new Torrent(generate_torrent_id());
			torrent->set_handle(a_temp->handle);
			torrents.push_back(torrent);
		}
		if (lt::alert_cast<lt::torrent_removed_alert>(a)) {
			lt::torrent_removed_alert const * a_temp = lt::alert_cast<lt::torrent_removed_alert>(a);
			// TODO - What should I do when this alert comes ?
			// the client who request using the API. Received an 202 Accepted as response since
			// the actual torrent removal is async. What if he needs to know wether the removal happened or not?
			// Should I store the info here in a data structure for future possible API requests or not ?
			// view GET /torrent/removed and DELETE /torrent/remove in restAPI.cpp. The /torrent/removed will get its info 
			// from here somehow
			// http://www.libtorrent.org/reference-Alerts.html#torrent-removed-alert
			//std::cout << a_temp->info_hash << " oibr " << std::endl << std::endl << std::endl;
		}
		if (lt::alert_cast<lt::torrent_deleted_alert>(a)) {
			lt::torrent_deleted_alert const * a_temp = lt::alert_cast<lt::torrent_deleted_alert>(a);
		}
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(200)); // TODO - get this out of here ASAP
}

std::vector<Torrent*>& TorrentManager::get_torrents() {
	return torrents;
}

// TODO - this method is really dumb and is temporary. Do something better to create ID. This is so bad that a collision with
// deleted torrent IDs may happen. If you delete the torrent with the highest ID and add a new one they will have the same ID =(
// They should be unique. 
unsigned int TorrentManager::generate_torrent_id() {
	return torrents.size()+1; 
}

TorrentManager::TorrentManager() {

}

TorrentManager::~TorrentManager() {
    // TODO destruct torrent handles. "all torrent_handles must be destructed before the session is destructed!"
    session.~session(); // TODO - make this async
}

// Marks the torrent for removal. Returns true if torrent is found and is marked for removal. Returns false if not.
// An torrent_deleted_alert is posted when the removal occurs 
bool TorrentManager::remove_torrent(int id, bool remove_data) {
	// TODO - this is sooooo unefficient. Use a Map instead of Vector to store torrents and change this code.
	for(std::vector<Torrent*>::iterator it = torrents.begin(); it != torrents.end(); it++) {
		if((*it)->get_id() == id) {
			session.remove_torrent((*it)->get_handle(), (int)remove_data);
			torrents.erase(it);
			return true;	
		}
	}	
	return false;
}
