#include <iostream>
#include <fstream>
#include "torrentManager.h"
#include "utility.h"
#include "plog/Log.h"
#include <fstream>
#include <sstream>
#include <typeinfo>

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

// TODO - improve logs and error handling where applicable
void TorrentManager::load_fast_resume(ConfigManager &config) {
	fs::path fastresume_path;
	try {
		fastresume_path = fs::path(config.get_config<std::string>("directory.fastresume_path"));
	}
	catch(const config_key_error &e) {
		LOG_ERROR << "Could not get config: " << e.what();
		return;
	}

	std::vector <fs::path> all_fastresume_files; 
	if(!get_files_in_folder(fastresume_path, ".fastresume", all_fastresume_files)) {
		LOG_ERROR << "Problem with fastresume directory " << fastresume_path.string() << ". Is this directory valid?";
		return;
	}

	for(fs::path fastresume_file : all_fastresume_files) {
		std::vector<char> fastresume_buffer;
		std::vector<char> torrent_buffer;

		{
			std::ifstream ifs;
			ifs.unsetf(std::ios_base::skipws);
			ifs.open(fastresume_file.c_str(), std::ios_base::binary);
			if(ifs.is_open()) {
				std::istream_iterator<char> start(ifs), end;
				fastresume_buffer = std::vector<char>(start, end);
			}
			else {
				LOG_ERROR << "Could not open fastresume file: " << fastresume_file.string();	
				continue;
			}
		}
		
		{
			fs::path torrent_file = "/mnt/DATA/Codacao/bitsleek/state/torrents/" + fastresume_file.stem().string() + ".torrent";
			if(!file_to_buffer(torrent_buffer, torrent_file.string())) {
				LOG_ERROR << "Could not open torrent file: " << torrent_file.string();	
				continue;
			}
		}

		char const *fastresume_buf = fastresume_buffer.data();
	       	lt::bdecode_node fastresume_node;	
		lt::error_code ec;
		int ret = lt::bdecode(fastresume_buf, fastresume_buf+fastresume_buffer.size(), fastresume_node, ec); 
		if(ec) {
			LOG_ERROR << "Problem occured while decoding fileresume buffer: " << ec.message();
			continue;
		}

		char const *torrent_buf = torrent_buffer.data();	
		lt::bdecode_node torrent_node;
		ec.clear();
		ret =  lt::bdecode(torrent_buf, torrent_buf+torrent_buffer.size(), torrent_node, ec);
		if(ec) {
			LOG_ERROR << "Problem occured while decoding torrent buffer: " << ec.message();
			continue;
		}

		ec.clear();
		lt::add_torrent_params atp = this->read_resume_data(fastresume_node, ec);
		// NOTE: using lt::add_torrent_params::resume_data is/will (???) be deprecated by libtorrent. The recommended way to 
		// load fastresume data is to use lt::read_resume_data(). This is currently (2017-12-11) only available in master branch, 
		// but maybe it will be available in future releases. Remember that. A few changes will be needed here.
		// More info here: https://github.com/arvidn/libtorrent/pull/1776
		atp.resume_data = fastresume_buffer;
		lt::torrent_info info(torrent_node);	
		boost::shared_ptr<lt::torrent_info> t_info = boost::make_shared<lt::torrent_info>(info);
		atp.ti = t_info;
		
		session.async_add_torrent(atp);

		LOG_DEBUG << "Fastresume torrent marked for asynchronous addition: " << fastresume_file.string();
	}
}

/* NOTE: (2017-12-14) This is a forked function from libtorrent master branch. This function is not in official releases yet, but I am using it
 here with some adaptations. When lt::read_resume_data() is finally on official releases use it instead and delete this.
 There will also be no more need to add resume_data buffer to atp.resume_data after calling this function. 
 I think (?) there will also be no need to set atp.ti, therefore dropping the need to maintain .torrent files in state folder MAYBE 
 look at read_resume_data() - https://github.com/arvidn/libtorrent/blob/6785046c2fefe6a997f6061e306d45c4f5058e56/src/read_resume_data.cpp
 Example - old style using atp::resume_data: https://github.com/arvidn/libtorrent/blob/62141036192954157469324cb2411e728c3f0851/examples/bt-get2.cpp
 Example - new style using lt::read_resume_data(): https://github.com/arvidn/libtorrent/blob/9e0a3aead1356a963f758ee95a4671d32f4617a7/examples/bt-get2.cpp
*/
lt::add_torrent_params TorrentManager::read_resume_data(lt::bdecode_node const& rd, lt::error_code& ec)
{
	lt::add_torrent_params ret;
	ret.save_path = rd.dict_find_string_value("save_path");

	return ret;
}
