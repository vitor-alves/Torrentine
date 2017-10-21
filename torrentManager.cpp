#include <iostream>
#include <fstream>
#include "torrentManager.h"
#include "utility.h"
#include "lib/plog/Log.h"

TorrentManager::TorrentManager() {
	greatest_id = 1;
}

TorrentManager::~TorrentManager() {
	for(Torrent* torrent : torrents) {
		delete torrent;
	}
	
	session.~session(); // This can be made async if necessary
}

bool TorrentManager::add_torrent_async(const std::string filename, const std::string save_path) {
	std::vector<char> buffer;
	if(!file_to_buffer(buffer, filename)) {
		LOG_ERROR << "A problem occured while adding torrent with filename " << filename;
		return false;	
	}
	
	lt::bdecode_node node;
	char const *buf = buffer.data();	
	lt::error_code ec;
	int ret =  lt::bdecode(buf, buf+buffer.size(), node, ec);
	if(ec) {
		LOG_ERROR << "Problem occured while decoding torrent buffer: " << ec.message().c_str();
		return false;
	}	
	lt::add_torrent_params atp;
	lt::torrent_info info(node);	
	boost::shared_ptr<lt::torrent_info> t_info = boost::make_shared<lt::torrent_info>(info);
	atp.ti = t_info;
	atp.save_path = save_path;
	session.async_add_torrent(atp);

	LOG_DEBUG << "Torrent with filename " << filename << " marked for addition asynchronously";
	return true;
}

/* Debug only */
void TorrentManager::update_torrent_console_view() {
	for(Torrent* torrent : torrents) {
		std::cout << "name: " << torrent->get_handle().status().name << " / ";
		std::cout << "hash: " << torrent->get_handle().status().info_hash << " / ";
		std::cout << "down: " << torrent->get_handle().status().download_rate / 1000 << "Kb/s" << " / ";
		std::cout << "up: " << torrent->get_handle().status().upload_rate / 1000 << "Kb/s" << " / ";
		std::cout << "prog: " << torrent->get_handle().status().progress * 100.0 << "%" << " / ";
		std::cout << "t_down: " << torrent->get_handle().status().total_download / 1000 << "Kb" << " / ";
		std::cout << "t_up: " << torrent->get_handle().status().total_upload / 1000 << "Kb" << " / ";
		std::cout << "seeds: " << torrent->get_handle().status().num_seeds<< " / ";
		std::cout << "peers: " << torrent->get_handle().status().num_peers << " / " << std::endl;
		std::cout << "ID: " << torrent->get_id() << " / " << std::endl;
	}	
	std::cout << std::endl;
}

void TorrentManager::check_alerts() {
	std::vector<lt::alert*> alerts;
	session.pop_alerts(&alerts);
	
	// TODO - needs optimizations. Cant just log errors and stay cool like nothing happened.	
	for (lt::alert const* a : alerts) {
		switch(a->type()) {
			case lt::torrent_finished_alert::alert_type:
			{
				break;
			}
			case lt::torrent_error_alert::alert_type:	
			{
				lt::torrent_error_alert const* a_temp = lt::alert_cast<lt::torrent_error_alert>(a);
				LOG_ERROR << "torrent_error_alert: " << a_temp->message();
				break;
			}
			case lt::add_torrent_alert::alert_type: 
			{
				lt::add_torrent_alert const* a_temp = lt::alert_cast<lt::add_torrent_alert>(a);
				if(a_temp->error) {
					LOG_ERROR << "add_torrent_alert: " << a_temp->message().c_str();
					break;
				}	
				Torrent* torrent = new Torrent(generate_torrent_id());
				torrent->set_handle(a_temp->handle);
				torrents.push_back(torrent);
				LOG_INFO << "add_torrent_alert: " << a_temp->message().c_str();
				break;
			}
			case lt::torrent_removed_alert::alert_type:
			{
				lt::torrent_removed_alert const * a_temp = lt::alert_cast<lt::torrent_removed_alert>(a);
				break;
			}
			case lt::torrent_deleted_alert::alert_type:
			{
		  		lt::torrent_deleted_alert const * a_temp = lt::alert_cast<lt::torrent_deleted_alert>(a);
				break;
			}
		}
	}	
}

const std::vector<Torrent*>& TorrentManager::get_torrents() {
	return torrents;
}

const unsigned long int TorrentManager::generate_torrent_id() {
	unsigned long int id = greatest_id;
	greatest_id++;
	return id; 
}

// Returns true if torrent is found and has been marked for removal. Returns false if not.
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
