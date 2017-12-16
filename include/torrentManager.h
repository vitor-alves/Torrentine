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
public:
	TorrentManager();
	~TorrentManager();
	bool add_torrent_async(const std::string filename,const std::string save_path);
	void check_alerts(lt::alert *a = NULL);
	void update_torrent_console_view();
	std::vector<std::shared_ptr<Torrent>> const & get_torrents();
	unsigned long int const generate_torrent_id();
	bool remove_torrent(unsigned long int id, bool remove_data);
	unsigned long int stop_torrents(const std::vector<unsigned long int> ids, bool force_stop);
	lt::alert const* wait_for_alert(lt::time_duration max_wait);
	bool save_session_state(ConfigManager &config);
	bool load_session_state(ConfigManager &config);
	void save_fastresume(ConfigManager &config, int resume_flags = lt::torrent_handle::save_info_dict);
	void load_fastresume(ConfigManager &config);
	void pause_session();
	bool load_session_settings(ConfigManager &config);
};

#endif
