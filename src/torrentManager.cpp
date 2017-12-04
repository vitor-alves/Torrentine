#include <iostream>
#include <fstream>
#include "torrentManager.h"
#include "utility.h"
#include "plog/Log.h"
#include <fstream>
#include <sstream>

// TODO - support settings_pack
TorrentManager::TorrentManager() {
	greatest_id = 1;
	outstanding_resume_data = 0;
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

bool TorrentManager::load_session_state(ConfigManager &config) {
	fs::path load_path = "./";
	try {
		load_path = fs::path(config.get_config<std::string>("directory.save_state_path"));
	}
	catch(config_key_error const &e) {
		LOG_ERROR << "Session state was not loaded. Could not get config: " << e.what();
		return false;
	}
	std::vector<char> buffer;
	bool success = file_to_buffer(buffer, load_path.string());
	if(!success) {
		LOG_ERROR << "Could not load session state file at " << load_path.string();
		return false;
	}
	lt::error_code ec;
	lt::bdecode_node node;
	char const *buf = buffer.data();
	lt::bdecode(buf, buf + buffer.size(), node, ec);
	session.load_state(node);
	return true;
}

bool TorrentManager::save_session_state(ConfigManager &config) {
	lt::entry e;
	session.save_state(e);
	std::filebuf fb;
	fs::path save_path = "./";
	try {
		save_path = fs::path(config.get_config<std::string>("directory.save_session_path"));
	}
	catch(config_key_error const &e) {
		LOG_ERROR << "Session state was not saved. Could not get config: " << e.what();
		return false;
	}

	fb.open(save_path.string(), std::ios::out);
	if(fb.is_open()) {
		std::ostream os(&fb);
		os << e;
		fb.close();
		LOG_INFO << "Session was saved at " << save_path.string();
		return true;
	}
	else {
		LOG_ERROR << "Session was not saved. Could not open save session file at " << save_path.string();
		return false;
	}
}

// TODO - incomplete - read documentation on libtorrent website! Read the notes! Very informative, specially about save resume 
// when a torrent goes to paused state, completed, etc. THere is also a note talking about full allocation and fast resume.
void TorrentManager::save_fast_resume() {
	// Pause session so fast resume data is valid
	session.pause(); // TODO - pause the session only when we are shutting down. In intermittent fast resume data save (every few minutes) 
			// There is not need to pause the session.
	for(std::shared_ptr<Torrent> torrent : torrents) {
		lt::torrent_handle h = torrent->get_handle();
		if(!h.is_valid())
			continue;
		lt::torrent_status s = h.status();
		if(!s.has_metadata)
			continue;
		if(!s.need_save_resume)
			continue;
		h.save_resume_data();
		outstanding_resume_data++;
	}

	while(outstanding_resume_data > 0) {
		lt::alert *a = session.wait_for_alert(std::chrono::seconds(10));

		if(a==0)
			break;

		std::vector<lt::alert*> alerts;
		session.pop_alerts(&alerts);

		for(lt::alert *a : alerts) {
			if(lt::alert_cast<lt::save_resume_data_failed_alert>(a)) {
				// TODO - do something about this
				outstanding_resume_data--;
				continue;
			}
		
			lt::save_resume_data_alert const *rd = lt::alert_cast<lt::save_resume_data_alert>(a);
			if(rd == 0) {
				// TODO - process alert in the main alert processing method. We cant ignore this alert.
				continue;
			}
	
			lt::torrent_handle h = rd->handle;
			lt::torrent_status ts = h.status(lt::torrent_handle::query_name);
			std::stringstream ss_name;
			ss_name << ts.info_hash;
			std::string file = "state/fastresume/"+ ss_name.str() +".fastresume";
			std::ofstream out(file.c_str(), std::ios_base::binary);
			out.unsetf(std::ios_base::skipws);
			lt::bencode(std::ostream_iterator<char>(out), *rd->resume_data);
			outstanding_resume_data--;
		}

	}
}

// TODO - read http://libtorrent.org/manual-ref.html#fast-resume specially the info field.
// line 65 - https://github.com/arvidn/libtorrent/blob/6785046c2fefe6a997f6061e306d45c4f5058e56/src/read_resume_data.cpp
// line 130 - https://github.com/arvidn/libtorrent/blob/6785046c2fefe6a997f6061e306d45c4f5058e56/test/test_read_resume.cpp
// line 174 - https://github.com/arvidn/libtorrent/blob/7730eea4011b75e700cadc385cdde52cc9f8a2ad/test/test_resume.cpp
// http://www.libtorrent.org/reference-Core.html
// http://www.libtorrent.org/manual-ref.html#fast-resume
void TorrentManager::load_fast_resume(ConfigManager &config) {
	fs::path fastresume_path;
	try {
		fastresume_path = fs::path(config.get_config<std::string>("directory.fastresume_path"));
	}
	catch(const config_key_error &e) {
		LOG_ERROR << "Could not get config: " << e.what();
		return;
	}
	std::vector <fs::path> fastresume_files; 
	if(!get_files_in_folder(fastresume_path, ".fastresume", fastresume_files)) {
		LOG_ERROR << "Problem with fastresume directory " << fastresume_path.string() << ". Is this directory valid?";
	}

	for(fs::path file : fastresume_files) {
		// TODO - Treat errors opening file	
		std::ifstream ifs(file.c_str(), std::ios_base::binary);
		ifs.unsetf(std::ios_base::skipws);
		std::istream_iterator<char> start(ifs), end;
		std::vector<char> buffer(start, end);
		char const *buf = buffer.data();	
		
		lt::bdecode_node node;
		lt::error_code ec;
		int ret =  lt::bdecode(buf, buf+buffer.size(), node, ec);
		if(ec) {
			LOG_ERROR << "Problem occured while decoding torrent buffer: " << ec.message();
			return;
		}
		lt::add_torrent_params atp;
		atp.name = node.dict_find_string_value("name");
		std::cout << "OiBR " << node.dict_find_string_value("file-format"); 
	
		session.async_add_torrent(atp);
	}

}
