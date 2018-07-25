#include <iostream>
#include <fstream>
#include "torrentManager.h"
#include "utility.h"
#include "plog/Log.h"
#include <fstream>
#include <sstream>
#include <typeinfo>
#include <libtorrent/extensions/ut_metadata.hpp>
#include <libtorrent/extensions/ut_pex.hpp>
#include <libtorrent/extensions/smart_ban.hpp>
#include <libtorrent/session_stats.hpp>

TorrentManager::TorrentManager(ConfigManager &config) : config(config) {
	greatest_id = 1;
	outstanding_resume_data = 0;
}

TorrentManager::~TorrentManager() {
	// This can be made async if necessary
	session.~session(); 
}

void TorrentManager::add_torrent_async(const lt::add_torrent_params &atp) {
	session.async_add_torrent(atp);
	
	// TODO - this is logging incorrectly!!
	LOG_INFO << "Torrent with filename " << atp.save_path << " marked for asynchronous addition";
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
		std::cout << "queue_pos: " << torrent->get_handle().status().queue_position << " / ";
		std::cout << std::endl;
	}
	std::cout << std::endl << std::endl;
}

void TorrentManager::check_alerts(lt::alert *a) {
	std::vector<lt::alert*> alerts;
	if(a == NULL) {
		session.pop_alerts(&alerts);
	}
	else {
		alerts.push_back(a);		
	}

	// TODO - There are a lot more alert messages that need to be here	
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
			case lt::session_stats_alert::alert_type:
			{
		  		lt::session_stats_alert const * a_temp = lt::alert_cast<lt::session_stats_alert>(a);
				SessionStatus last_session_status = session_status;

				auto interval = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now() - interval_last_point).count();

				int const index_has_incoming_connections = lt::find_metric_idx("net.has_incoming_connections");
				if(index_has_incoming_connections != -1) {
					session_status.has_incoming_connections = a_temp->values[index_has_incoming_connections];
				}

				int const index_sent_bytes = lt::find_metric_idx("net.sent_bytes");
				if(index_sent_bytes != -1) {
					session_status.total_upload = a_temp->values[index_sent_bytes];
					session_status.upload_rate = (session_status.total_upload - last_session_status.total_upload)/(interval);				
				}

				int const index_recv_bytes = lt::find_metric_idx("net.recv_bytes");
				if(index_recv_bytes != -1) {
					session_status.total_download = a_temp->values[index_recv_bytes];
					session_status.download_rate = (session_status.total_download - last_session_status.total_download)/(interval);				
				}

				int const index_sent_ip_overhead_bytes = lt::find_metric_idx("net.sent_ip_overhead_bytes");
				if(index_sent_ip_overhead_bytes!= -1) {
					session_status.ip_overhead_upload = a_temp->values[index_sent_ip_overhead_bytes];
					session_status.ip_overhead_upload_rate = (session_status.ip_overhead_upload - last_session_status.ip_overhead_upload)/(interval);				
				}

				int const index_recv_ip_overhead_bytes = lt::find_metric_idx("net.recv_ip_overhead_bytes");
				if(index_recv_ip_overhead_bytes!= -1) {
					session_status.ip_overhead_download = a_temp->values[index_recv_ip_overhead_bytes];
					session_status.ip_overhead_download_rate = (session_status.ip_overhead_download - last_session_status.ip_overhead_download)/(interval);				
				}

				int const index_dht_bytes_out = lt::find_metric_idx("dht.dht_bytes_out");
				if(index_dht_bytes_out!= -1) {
					session_status.dht_upload = a_temp->values[index_dht_bytes_out];
					session_status.dht_upload_rate = (session_status.dht_upload - last_session_status.dht_upload)/(interval);				
				}

				int const index_dht_bytes_in = lt::find_metric_idx("dht.dht_bytes_in");
				if(index_dht_bytes_in!= -1) {
					session_status.dht_download = a_temp->values[index_dht_bytes_in];
					session_status.dht_download_rate = (session_status.dht_download - last_session_status.dht_download)/(interval);				
				}

				int const index_dht_nodes = lt::find_metric_idx("dht.dht_nodes");
				if(index_dht_nodes!= -1) {
					session_status.dht_nodes = a_temp->values[index_dht_nodes];
				}

				int const index_tracker_upload = lt::find_metric_idx("net.sent_tracker_bytes");
				if(index_tracker_upload!= -1) {
					session_status.tracker_upload = a_temp->values[index_tracker_upload];
					session_status.tracker_upload_rate = (session_status.tracker_upload - last_session_status.tracker_upload)/(interval);				
				}

				int const index_tracker_download = lt::find_metric_idx("net.recv_tracker_bytes");
				if(index_tracker_download!= -1) {
					session_status.tracker_download = a_temp->values[index_tracker_download];
					session_status.tracker_download_rate = (session_status.tracker_download - last_session_status.tracker_download)/(interval);				
				}

				int const index_sent_payload_bytes = lt::find_metric_idx("net.sent_payload_bytes");
				if(index_sent_payload_bytes != -1) {
					session_status.total_payload_upload = a_temp->values[index_sent_payload_bytes];
					session_status.payload_upload_rate = (session_status.total_payload_upload - last_session_status.total_payload_upload)/(interval);				
				}

				int const index_recv_payload_bytes = lt::find_metric_idx("net.recv_payload_bytes");
				if(index_recv_payload_bytes != -1) {
					session_status.total_payload_download = a_temp->values[index_recv_payload_bytes];
					session_status.payload_download_rate = (session_status.total_payload_download - last_session_status.total_payload_download)/(interval);				
				}

				int const index_num_peers_connected = lt::find_metric_idx("peer.num_peers_connected");
				int const index_num_peers_half_open = lt::find_metric_idx("peer.num_peers_half_open");
				if(index_num_peers_connected != -1 && index_num_peers_half_open != -1) {
					session_status.total_peers_connections = a_temp->values[index_num_peers_connected] + a_temp->values[index_num_peers_half_open];
				}
				
				interval_last_point = std::chrono::steady_clock::now();
				break;
			}
		}
	}	
}

// TODO - function name in incorrect format
// TODO - why there are 2 functions like this ? one called get_torrents_status and one called get_status_torrents
unsigned long int TorrentManager::get_torrents_status(std::vector<lt::torrent_status> &torrents_status, std::vector<unsigned long int> ids) {

	// No ids specified. Get all torrents status
	if(ids.size() == 0) {
		for(std::vector<std::shared_ptr<Torrent>>::iterator it = torrents.begin(); it != torrents.end(); it++) {
			torrents_status.push_back((*it)->get_handle().status()); // TODO - create get torrent status function
		}
		return 0;
	}

	// TODO - This in O(n) and not necessary. Embbed this in the for loop below that adds the items to the vector. When not found, return the id.
	// Check if it exists on the fly 
	// Check if all torrents in ids in fact exist
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

	// Get torrent status in ids
	for(unsigned long int id : ids) {
		for(std::vector<std::shared_ptr<Torrent>>::iterator it = torrents.begin(); it != torrents.end(); it++) {
			if((*it)->get_id() == id) {
				torrents_status.push_back((*it)->get_handle().status());
			}
		}
	}


	return 0;
}

unsigned long int const TorrentManager::generate_torrent_id() {
	return greatest_id++; 
}

// An torrent_deleted_alert is posted when the removal occurs 
unsigned long int TorrentManager::remove_torrent(const std::vector<unsigned long int> ids, bool remove_data) {
	// TODO - this is sooooo inefficient. Use a Map instead of Vector to store torrents and change this code.
	
	// Check if all torrents in ids in fact exist
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


	for(unsigned long int id : ids) {
		for(std::vector<std::shared_ptr<Torrent>>::iterator it = torrents.begin(); it != torrents.end(); it++) {
			if((*it)->get_id() == id) {
				lt::torrent_handle handle = (*it)->get_handle();
				session.remove_torrent(handle, remove_data);
				(*it).reset();
				torrents.erase(it);
				break;	
			}
		}
	}
	return 0;
}

unsigned long int TorrentManager::recheck_torrents(const std::vector<unsigned long int> ids) {

	// No ids specified. Recheck all torrents
	if(ids.size() == 0) {
		for(std::vector<std::shared_ptr<Torrent>>::iterator it = torrents.begin(); it != torrents.end(); it++) {
			lt::torrent_handle handle = (*it)->get_handle();
			handle.force_recheck();
		}
		return 0;
	}

	// Check if all torrents in ids in fact exist
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

	// Recheck torrents in ids
	for(unsigned long int id : ids) {
		for(std::vector<std::shared_ptr<Torrent>>::iterator it = torrents.begin(); it != torrents.end(); it++) {
			if((*it)->get_id() == id) {
				lt::torrent_handle handle = (*it)->get_handle();
				handle.force_recheck();
			}
		}
	}
	return 0;
}

unsigned long int TorrentManager::stop_torrents(const std::vector<unsigned long int> ids, bool force_stop) {

	// No ids specified. Stop all torrents
	if(ids.size() == 0) {
		for(std::vector<std::shared_ptr<Torrent>>::iterator it = torrents.begin(); it != torrents.end(); it++) {
			lt::torrent_handle handle = (*it)->get_handle();
			if(force_stop)
				handle.pause();
			else
				handle.pause(lt::torrent_handle::graceful_pause);
		}
		return 0;
	}

	// Check if all torrents in ids in fact exist
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

	// Stop torrents in ids
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

std::vector<unsigned long int> TorrentManager::get_all_ids() {
	std::vector<unsigned long int> ids;
	for(std::shared_ptr<Torrent> t : torrents) {
		ids.push_back(t->get_id());
	}
	return ids;
}

unsigned long int TorrentManager::get_files_torrents(std::vector<std::vector<Torrent::torrent_file>> &torrent_files, const std::vector<unsigned long int> ids, bool piece_granularity) {

	// TODO - put this check in a function and use it in all other API methods to reduce redundancy
	// Check if all torrents in ids in fact exist
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

	// Get files from torrents in ids
	for(unsigned long int id : ids) {
		for(std::vector<std::shared_ptr<Torrent>>::iterator it = torrents.begin(); it != torrents.end(); it++) {
			if((*it)->get_id() == id) {
				std::vector<Torrent::torrent_file> tf = (*it)->get_torrent_files(piece_granularity);
				torrent_files.push_back(tf);
			}
		}
	}

	return 0;
}

unsigned long int TorrentManager::get_trackers_torrents(std::vector<std::vector<lt::announce_entry>> &torrent_trackers, const std::vector<unsigned long int> ids) {
	// TODO - put this check in a function and use it in all other API methods to reduce redundancy
	// Check if all torrents in ids in fact exist
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

	// Get trackers from torrents in ids
	for(unsigned long int id : ids) {
		for(std::vector<std::shared_ptr<Torrent>>::iterator it = torrents.begin(); it != torrents.end(); it++) {
			if((*it)->get_id() == id) {
				std::vector<lt::announce_entry> tt = (*it)->get_torrent_trackers();
				torrent_trackers.push_back(tt);
			}
		}
	}

	return 0;
}

unsigned long int TorrentManager::get_peers_torrents(std::vector<std::vector<Torrent::torrent_peer>> &torrent_peers, const std::vector<unsigned long int> ids) {
	// TODO - put this check in a function and use it in all other API methods to reduce redundancy
	// Check if all torrents in ids in fact exist
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

	// Get peers from torrents in ids
	for(unsigned long int id : ids) {
		for(std::vector<std::shared_ptr<Torrent>>::iterator it = torrents.begin(); it != torrents.end(); it++) {
			if((*it)->get_id() == id) {
				std::vector<Torrent::torrent_peer> tp = (*it)->get_torrent_peers();
				torrent_peers.push_back(tp);
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

bool TorrentManager::load_session_state() {
	fs::path load_path;
	try {
		load_path = fs::path(config.get_config<std::string>("directory.session_state_path"));
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
	LOG_DEBUG << "Session state loaded";
	return true;
}

bool TorrentManager::save_session_state() {
	lt::entry e;
	session.save_state(e);
	std::filebuf fb;
	fs::path save_path;
	try {
		save_path = fs::path(config.get_config<std::string>("directory.session_state_path"));
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
		LOG_INFO << "Session state was saved at " << save_path.string();
		return true;
	}
	else {
		LOG_ERROR << "Session state was not saved. Could not open save session state file at " << save_path.string();
		return false;
	}
}

void TorrentManager::save_fastresume(int resume_flags) {
	fs::path fastresume_path;
	
	try {
		fastresume_path = fs::path(config.get_config<std::string>("directory.fastresume_path"));
	}
	catch(const config_key_error &e) {
		LOG_ERROR << "Fastresume not saved. Could not get config: " << e.what();
		return;
	}
	
	for(std::shared_ptr<Torrent> torrent : torrents) {
		lt::torrent_handle h = torrent->get_handle();
		if(!h.is_valid())
			continue;
		lt::torrent_status s = h.status();
		if(!s.has_metadata)
			continue;
		if(!s.need_save_resume)
			continue;
		h.save_resume_data(resume_flags);
		outstanding_resume_data++;
	}

	while(outstanding_resume_data > 0) {
		lt::alert *a = session.wait_for_alert(std::chrono::seconds(10));

		if(a==0) {
			LOG_DEBUG << "Wait for resume data alert timed out";
			break;
		}

		std::vector<lt::alert*> alerts;
		session.pop_alerts(&alerts);

		for(lt::alert *a : alerts) {
			if(lt::alert_cast<lt::save_resume_data_failed_alert>(a)) {
				LOG_ERROR << "Save fastresume data failed: " << a->message();
				outstanding_resume_data--;
				continue;
			}
		
			lt::save_resume_data_alert const *rd = lt::alert_cast<lt::save_resume_data_alert>(a);
			if(rd == 0) {
				check_alerts(a);
				continue;
			}
				
			outstanding_resume_data--;
			lt::torrent_handle h = rd->handle;
			lt::torrent_status ts = h.status(lt::torrent_handle::query_name);
			std::stringstream ss_name;
			ss_name << ts.info_hash;
			fs::path out_file = fastresume_path.string()+ ss_name.str() +".fastresume";
			std::ofstream out(out_file.c_str(), std::ios_base::binary);
			out.unsetf(std::ios_base::skipws);
			if(out.is_open()) {
				lt::bencode(std::ostream_iterator<char>(out), *rd->resume_data);
				LOG_DEBUG << "Saved fastresume: " << out_file;
			}
			else {
				LOG_ERROR << "Could not save fastresume: " << out_file;
			}
		}
	}
}

void TorrentManager::load_fastresume() {
	fs::path fastresume_path;
	try {
		fastresume_path = fs::path(config.get_config<std::string>("directory.fastresume_path"));
	}
	catch(const config_key_error &e) {
		LOG_ERROR << "Fastresume not loaded. Could not get config: " << e.what();
		return;
	}

	std::vector <fs::path> all_fastresume_files; 
	if(!get_files_in_folder(fastresume_path, ".fastresume", all_fastresume_files)) {
		LOG_ERROR << "Problem with fastresume directory " << fastresume_path.string() << ". Is this directory valid?";
		return;
	}

	for(fs::path fastresume_file : all_fastresume_files) {
		std::vector<char> fastresume_buffer;

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

	       	lt::bdecode_node fastresume_node;	
		lt::error_code ec;
		char const *fastresume_buf = fastresume_buffer.data();
		int ret = lt::bdecode(fastresume_buf, fastresume_buf+fastresume_buffer.size(), fastresume_node, ec); 
		if(ec) {
			LOG_ERROR << "Problem occured while decoding fastresume buffer: " << ec.message();
			continue;
		}

		ec.clear();
		lt::add_torrent_params atp = this->read_resume_data(fastresume_node, ec);
		/* NOTE: using lt::add_torrent_params::resume_data is/will (???) be deprecated by libtorrent. The recommended way to 
		 load fastresume data is to use lt::read_resume_data(). This is currently (2017-12-11) only available in master branch, 
		 but maybe it will be available in future releases. Remember that. A few changes will be needed here.
		 More info here: https://github.com/arvidn/libtorrent/pull/1776 */
		atp.resume_data = fastresume_buffer;
		
		session.async_add_torrent(atp);

		LOG_DEBUG << "Fastresume torrent marked for asynchronous addition: " << fastresume_file.string();
	}
}

/* NOTE: (2017-12-14) This is a forked function from libtorrent master branch. This function is not in official releases yet, but I am using it
 here with some adaptations. When lt::read_resume_data() is finally on official releases use it instead and delete this.
 There will also be no more need to add resume_data buffer to atp.resume_data after calling this function. 
 look at read_resume_data() - https://github.com/arvidn/libtorrent/blob/6785046c2fefe6a997f6061e306d45c4f5058e56/src/read_resume_data.cpp
 Example - old style using atp::resume_data: https://github.com/arvidn/libtorrent/blob/62141036192954157469324cb2411e728c3f0851/examples/bt-get2.cpp
 Example - new style using lt::read_resume_data(): https://github.com/arvidn/libtorrent/blob/9e0a3aead1356a963f758ee95a4671d32f4617a7/examples/bt-get2.cpp */
lt::add_torrent_params TorrentManager::read_resume_data(lt::bdecode_node const& rd, lt::error_code& ec)
{
	lt::add_torrent_params ret;
	ret.save_path = rd.dict_find_string_value("save_path");

	return ret;
}

void TorrentManager::pause_session() {
	session.pause();
}


void TorrentManager::load_session_settings() {
	
	// TODO - use others settings too. I will probably need to store settings in config file.	
	lt::settings_pack pack;
	pack.set_str(lt::settings_pack::user_agent, "Torrentine 0.0.0"); // TODO - use global variable bitsleek_version
	pack.set_int(lt::settings_pack::active_downloads, 5); // TODO - put this in config.ini (maybe this option is alreary in fastresume)
	
	session.apply_settings(pack);
	LOG_DEBUG << "Loaded session settings";
}

void TorrentManager::load_session_extensions() {
	bool ut_metadata_plugin_enabled;
	bool ut_pex_plugin_enabled;
	bool smart_ban_plugin_enabled;
	try {
		(config.get_config<std::string>("libtorrent.extensions.ut_metadata_plugin") == "enabled") ?
			ut_metadata_plugin_enabled = true : ut_metadata_plugin_enabled = false; 

		(config.get_config<std::string>("libtorrent.extensions.ut_pex_plugin") == "enabled") ?
			ut_pex_plugin_enabled = true : ut_pex_plugin_enabled = false; 
	
		(config.get_config<std::string>("libtorrent.extensions.smart_ban_plugin") == "enabled") ?
			smart_ban_plugin_enabled = true : smart_ban_plugin_enabled = false; 
	}
	catch(const config_key_error &e) {
		LOG_ERROR << "Libtorrent extensions were not loaded. Could not get config: " << e.what();
		return;
	}
	
	std::stringstream log_msg;
	log_msg << "Loaded Libtorrent extensions: ";
	if(ut_metadata_plugin_enabled) {
		session.add_extension(&lt::create_ut_metadata_plugin);	
		log_msg << "ut_metadata_plugin ";
	}
	if(ut_pex_plugin_enabled) {
		session.add_extension(&lt::create_ut_pex_plugin);	
		log_msg << " ut_pex_plugin ";
	}
	if(smart_ban_plugin_enabled) {
		session.add_extension(&lt::create_smart_ban_plugin);	
		log_msg << " smart_ban_plugin";
	}
	LOG_DEBUG << log_msg.str();
}

unsigned long int TorrentManager::start_torrents(const std::vector<unsigned long int> ids) {

	// No ids specified. Start all torrents
	if(ids.size() == 0) {
		for(std::vector<std::shared_ptr<Torrent>>::iterator it = torrents.begin(); it != torrents.end(); it++) {
			lt::torrent_handle handle = (*it)->get_handle();
				handle.resume();
		}
		return 0;
	}

	// Check if all torrents in ids in fact exist
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

	// Start torrents in ids
	for(unsigned long int id : ids) {
		for(std::vector<std::shared_ptr<Torrent>>::iterator it = torrents.begin(); it != torrents.end(); it++) {
			if((*it)->get_id() == id) {
				lt::torrent_handle handle = (*it)->get_handle();
					handle.resume();
			}
		}
	}
	return 0;
}

unsigned long int TorrentManager::get_status_torrents(std::vector<lt::torrent_status> &torrent_status, const std::vector<unsigned long int> ids) {
	// TODO - put this check in a function and use it in all other API methods to reduce redundancy
	// Check if all torrents in ids in fact exist
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

	// Get status from torrents in ids
	for(unsigned long int id : ids) {
		for(std::vector<std::shared_ptr<Torrent>>::iterator it = torrents.begin(); it != torrents.end(); it++) {
			if((*it)->get_id() == id) {
				lt::torrent_status status = (*it)->get_torrent_status();
				torrent_status.push_back(status);
			}
		}
	}

	return 0;
}


void TorrentManager::post_session_stats() {
	session.post_session_stats();
}

SessionStatus const TorrentManager::get_session_status() {
	return session_status;

}

lt::settings_pack const TorrentManager::get_session_settings() {
	return session.get_settings();

}

unsigned long int TorrentManager::get_torrents_info(std::vector<boost::shared_ptr<const lt::torrent_info>> &torrents_info, const std::vector<unsigned long int> ids) {
	// TODO - put this check in a function and use it in all other API methods to reduce redundancy
	// Check if all torrents in ids in fact exist
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

	// Get info from torrents in ids
	for(unsigned long int id : ids) {
		for(std::vector<std::shared_ptr<Torrent>>::iterator it = torrents.begin(); it != torrents.end(); it++) {
			if((*it)->get_id() == id) {
				boost::shared_ptr<const lt::torrent_info> ti = (*it)->get_torrent_info();
				torrents_info.push_back(ti);
			}
		}
	}

	return 0;
}

unsigned long int TorrentManager::get_settings_torrents(std::vector<Torrent::torrent_settings> &torrent_settings, const std::vector<unsigned long int> ids) {

	// TODO - put this check in a function and use it in all other API methods to reduce redundancy
	// Check if all torrents in ids in fact exist
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

	// Get settings from torrents in ids
	for(unsigned long int id : ids) {
		for(std::vector<std::shared_ptr<Torrent>>::iterator it = torrents.begin(); it != torrents.end(); it++) {
			if((*it)->get_id() == id) {
				Torrent::torrent_settings ts = (*it)->get_torrent_settings();
				torrent_settings.push_back(ts);
			}
		}
	}

	return 0;
}

unsigned long int TorrentManager::set_settings_torrents(std::vector<Torrent::torrent_settings> &torrent_settings, const std::vector<unsigned long int> ids) {

	// TODO - put this check in a function and use it in all other API methods to reduce redundancy
	// Check if all torrents in ids in fact exist
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

	// Set settings for torrents in ids
	int index = 0;
	for(unsigned long int id : ids) {
		for(std::vector<std::shared_ptr<Torrent>>::iterator it = torrents.begin(); it != torrents.end(); it++) {
			if((*it)->get_id() == id) {
				(*it)->set_torrent_settings(torrent_settings.at(index));
			}
		}
		index++;
	}

	return 0;
}
