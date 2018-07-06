#include <libtorrent/session.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/lazy_entry.hpp>
#include <libtorrent/peer_info.hpp>
#include "libtorrent/session_handle.hpp"
#include "libtorrent/hasher.hpp"
#include <boost/filesystem.hpp>
#include "torrent.h"
#include "config.h"

#ifndef TORRENT_MANAGER_H
#define TORRENT_MANAGER_H

namespace lt = libtorrent;
namespace fs = boost::filesystem;

class TorrentManager {
	
private:
	lt::session session;
	std::vector<std::shared_ptr<Torrent>> torrents;
	unsigned long int greatest_id;
	unsigned long int outstanding_resume_data;
	lt::add_torrent_params read_resume_data(lt::bdecode_node const& rd, lt::error_code& ec);
	ConfigManager &config;
public:
	TorrentManager(ConfigManager &config);
	~TorrentManager();
	bool add_torrent_async(const lt::add_torrent_params &atp);
	void check_alerts(lt::alert *a = NULL);
	void update_torrent_console_view();
	unsigned long int get_torrents_status(std::vector<lt::torrent_status> &torrents_status, std::vector<unsigned long int> ids);
	unsigned long int const generate_torrent_id();
	unsigned long int remove_torrent(const std::vector<unsigned long int> ids, bool remove_data);
	unsigned long int stop_torrents(const std::vector<unsigned long int> ids, bool force_stop);
	std::vector<unsigned long int> get_all_ids();
	unsigned long int get_files_torrents(std::vector<std::vector<Torrent::torrent_file>> &torrent_files, const std::vector<unsigned long int> ids, bool piece_granularity);
	unsigned long int get_peers_torrents(std::vector<std::vector<Torrent::torrent_peer>> &torrent_peers, const std::vector<unsigned long int> ids);
	unsigned long int get_status_torrents(std::vector<lt::torrent_status> &torrent_status, const std::vector<unsigned long int> ids);
	unsigned long int recheck_torrents(const std::vector<unsigned long int> ids);
	unsigned long int start_torrents(const std::vector<unsigned long int> ids);	
	lt::alert const* wait_for_alert(lt::time_duration max_wait);
	bool save_session_state();
	bool load_session_state();
	void save_fastresume(int resume_flags = lt::torrent_handle::save_info_dict);
	void load_fastresume();
	void pause_session();
	void load_session_settings();
	void load_session_extensions();
};

#endif
