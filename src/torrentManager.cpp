#include <iostream>
#include <fstream>
#include "torrentManager.h"
#include "utility.h"
#include "plog/Log.h"

TorrentManager::TorrentManager() {
	greatest_id = 1;
}

TorrentManager::~TorrentManager() {
	// This can be made async if necessary
	session.~session(); 
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
		LOG_ERROR << "Problem occured while decoding torrent buffer: " << ec.message();
		return false;
	}	
	lt::add_torrent_params atp;
	lt::torrent_info info(node);	
	boost::shared_ptr<lt::torrent_info> t_info = boost::make_shared<lt::torrent_info>(info);
	atp.ti = t_info;
	atp.save_path = save_path;
	session.async_add_torrent(atp);

	LOG_INFO << "Torrent with filename " << filename << " marked for asynchronous addition";
	return true;
}

/* Debug only */
void TorrentManager::update_torrent_console_view() {
	for(std::shared_ptr<Torrent> torrent : torrents) {
		std::cout << "id: " << torrent->get_id() << " / ";
		std::cout << "name: " << torrent->get_handle().status().name << " / ";
		//std::cout << "hash: " << torrent->get_handle().status().info_hash << " / ";
		std::cout << "down: " << torrent->get_handle().status().download_rate / 1000 << "Kb/s" << " / ";
		std::cout << "up: " << torrent->get_handle().status().upload_rate / 1000 << "Kb/s" << " / ";
		std::cout << "p: " << torrent->get_handle().status().progress * 100.0 << "%" << " / ";
		std::cout << "t_down: " << torrent->get_handle().status().total_download / 1000 << "Kb" << " / ";
		std::cout << "t_up: " << torrent->get_handle().status().total_upload / 1000 << "Kb" << " / ";
		std::cout << "seeds: " << torrent->get_handle().status().num_seeds<< " / ";
		std::cout << "peers: " << torrent->get_handle().status().num_peers << " / ";
		std::cout << "paused: " << torrent->get_handle().status().paused << " / ";
		std::cout << std::endl;
	}
	std::cout << std::endl << std::endl;
}

void TorrentManager::check_alerts() {
	std::vector<lt::alert*> alerts;
	session.pop_alerts(&alerts);
	
	// TODO - needs optimizations. Cant just log errors and stay cool like nothing happened.
	// There are a lot more alert messages that need to be here	
	for (lt::alert const *a : alerts) {
		switch(a->type()) {
			case lt::torrent_finished_alert::alert_type:
			{
				break;
			}
			case lt::torrent_error_alert::alert_type:	
			{
				lt::torrent_error_alert const* a_temp = lt::alert_cast<lt::torrent_error_alert>(a);
				LOG_ERROR << "torrent_error_alert: " << a_temp->error.message();
				break;
			}
			case lt::add_torrent_alert::alert_type: 
			{
				lt::add_torrent_alert const* a_temp = lt::alert_cast<lt::add_torrent_alert>(a);
				if(a_temp->error) {
					LOG_ERROR << "add_torrent_alert: " << a_temp->error.message();
					break;
				}	
				std::shared_ptr<Torrent> torrent = std::make_shared<Torrent>(generate_torrent_id());
				torrent->set_handle(a_temp->handle);
				torrents.push_back(torrent);
				LOG_INFO << "add_torrent_alert: " << a_temp->message();
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
			case lt::torrent_paused_alert::alert_type:
			{
		  		lt::torrent_paused_alert const * a_temp = lt::alert_cast<lt::torrent_paused_alert>(a);
				break;
			}
		}
	}	
}

std::vector<std::shared_ptr<Torrent>> const & TorrentManager::get_torrents() {
	return torrents;
}

unsigned long int const TorrentManager::generate_torrent_id() {
	return greatest_id++; 
}

// An torrent_deleted_alert is posted when the removal occurs 
bool TorrentManager::remove_torrent(const unsigned long int id, bool remove_data) {
	// TODO - this is sooooo inefficient. Use a Map instead of Vector to store torrents and change this code.
	for(std::vector<std::shared_ptr<Torrent>>::iterator it = torrents.begin(); it != torrents.end(); it++) {
		if((*it)->get_id() == id) {
			lt::torrent_handle handle = (*it)->get_handle();
			session.remove_torrent(handle, remove_data);
			(*it).reset();
			torrents.erase(it);
			return true;	
		}
	}	
	return false;
}

// TODO - ids should be optional. If not present: stop all.
unsigned long int TorrentManager::stop_torrents(const std::vector<unsigned long int> ids, bool force_stop) {

	// TODO - this is sooooo inefficient. Use a Map instead of Vector to store torrents and change this code.
	// Too dumb. fix this.
	for(unsigned long int id : ids) {
		bool found = false;
		for(std::vector<std::shared_ptr<Torrent>>::iterator it = torrents.begin(); it != torrents.end(); it++) {
			if((*it)->get_id() == id) {
				found = true;
			}
		}
		if(!found)
			return id;
	}

	// TODO - this is sooooo inefficient. Use a Map instead of Vector to store torrents and change this code.
	for(unsigned long int id : ids) {
		for(std::vector<std::shared_ptr<Torrent>>::iterator it = torrents.begin(); it != torrents.end(); it++) {
			if((*it)->get_id() == id) {
				lt::torrent_handle handle = (*it)->get_handle();
				if(force_stop)
					handle.pause();
				else
					handle.pause(lt::torrent_handle::graceful_pause);
			}
		}
	}
	return 0;
}

// Reminder: It returns the alert but does not pop it from the queue
lt::alert const* TorrentManager::wait_for_alert(lt::time_duration max_wait) {
	lt::alert const* a = session.wait_for_alert(max_wait);
	return a;
}

// TODO - Improve this ASAP
void TorrentManager::load_state() {
	std::ifstream ifs;
	ifs.open("state/session.dat");
	std::filebuf *fb = ifs.rdbuf();
	std::size_t size = fb->pubseekoff(0, ifs.end, ifs.in);
	fb->pubseekpos(0, ifs.in);
	lt::lazy_entry e;
	lt::error_code ec;
	char *buffer = new char[size];
	fb->sgetn(buffer, size);
	ifs.close();
	lt::bdecode_node node;
	lt::bdecode(buffer, buffer + size, node, ec);
	LOG_DEBUG << buffer;
	session.load_state(node);
	delete[] buffer;
}

// TODO - improve
void TorrentManager::save_session() {
	lt::entry e;
	session.save_state(e);
	std::filebuf fb;
	fb.open("state/session.dat", std::ios::out);
	std::ostream os(&fb);
	os << e;
	fb.close();
}
