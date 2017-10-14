#include <libtorrent/session.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/bencode.hpp>
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
	void add_torrent_async(const std::string filename,const std::string save_path);
	void check_alerts();
	void update_torrent_console_view();
	const std::vector<Torrent*>& get_torrents();
	const unsigned int generate_torrent_id();
	bool remove_torrent(unsigned int id, bool remove_data);
};

#endif
