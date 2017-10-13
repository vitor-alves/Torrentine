#include <libtorrent/session.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/lazy_entry.hpp>
#include <libtorrent/lazy_entry.hpp>
#include <libtorrent/peer_info.hpp>
#include "torrent.h"

#ifndef TORRENT_MANAGER_H
#define TORRENT_MANAGER_H

namespace lt = libtorrent;

class TorrentManager {
private:
	lt::session session;
	std::vector<Torrent*> torrents;	

public:
	TorrentManager();
	~TorrentManager();
	void add_torrent_async(std::string filename, std::string save_path);
	void check_alerts();
	void update_torrent_console_view(); // Deprecated
	std::vector<Torrent*>& get_torrents();
};

#endif
