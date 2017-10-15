#include <iostream>
#include <fstream>
#include "torrentManager.h"

TorrentManager::TorrentManager() {
	greatest_id = 1;
}

TorrentManager::~TorrentManager() {
	for(Torrent* torrent : torrents) {
		delete torrent;
	}

	// This can be made async if needed.
	session.~session();
}

// TODO - treat errors on error_code, finding file etc
/* Add torrent asynchronously by .torrent filename */
void TorrentManager::add_torrent_async(const std::string filename, const std::string save_path) {
	/* Read file to buffer */
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

	/* Add torrent to session */
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

/* Debug only. Prints torrent stats to console */
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

/* Checks for new alerts and reacts to them */ 
void TorrentManager::check_alerts() {
	std::vector<lt::alert*> alerts;
	session.pop_alerts(&alerts);
	
	/* For each new alert, do something based on its type */
	for (lt::alert const* a : alerts) {
		//std::cout << a->message() << "\n" << std::endl;
		/* Finished */
		if (lt::alert_cast<lt::torrent_finished_alert>(a)) {
		
		}
		/* Error */
		else if (lt::alert_cast<lt::torrent_error_alert>(a)) {
			std::cout << "ALERT ERROR" << std::endl; // TODO - log
			exit(1); // TODO - come on, I can do better than this
		}
		/* Add torrent */
		else if (lt::alert_cast<lt::add_torrent_alert>(a)) {
			lt::add_torrent_alert const * a_temp = lt::alert_cast<lt::add_torrent_alert>(a);
			
			/* Create a Torrent and add it to torrents */
			Torrent* torrent = new Torrent(generate_torrent_id());
			torrent->set_handle(a_temp->handle);
			torrents.push_back(torrent);
		}
		/* Torrent removed */
		else if (lt::alert_cast<lt::torrent_removed_alert>(a)) {
			lt::torrent_removed_alert const * a_temp = lt::alert_cast<lt::torrent_removed_alert>(a);
			// TODO - log this
		}
		/* Torrent deleted */
		if (lt::alert_cast<lt::torrent_deleted_alert>(a)) {
			lt::torrent_deleted_alert const * a_temp = lt::alert_cast<lt::torrent_deleted_alert>(a);
		}
	}	
}

/* Getter - torrents */
const std::vector<Torrent*>& TorrentManager::get_torrents() {
	return torrents;
}

const unsigned long int TorrentManager::generate_torrent_id() {
	unsigned long int id = greatest_id;
	greatest_id++;
	return id; 
}

// Marks the torrent for removal. Returns true if torrent is found and is marked for removal. Returns false if not.
// An torrent_deleted_alert is posted when the removal occurs 
bool TorrentManager::remove_torrent(const unsigned long int id, bool remove_data) {
	// TODO - this is sooooo unefficient. Use a Map instead of Vector to store torrents and change this code.
	for(std::vector<Torrent*>::iterator it = torrents.begin(); it != torrents.end(); it++) {
		if((*it)->get_id() == id) {
			lt::torrent_handle handle = (*it)->get_handle();
			session.remove_torrent(handle, remove_data);
			delete *it;
			torrents.erase(it);
			return true;	
		}
	}	
	return false;
}
