#include <libtorrent/session.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/lazy_entry.hpp>
#include <libtorrent/lazy_entry.hpp>
#include <libtorrent/peer_info.hpp>

#ifndef TORRENT_MANAGER_H
#define TORRENT_MANAGER_H

namespace lt = libtorrent;

class TorrentManager {
private:
	lt::session session;
	std::vector<lt::torrent_handle> torrent_handle_vector;

public:
	TorrentManager();
	~TorrentManager();
	void add_torrent_async(std::string filename, std::string save_path);
	void check_alerts();
	void update_torrent_console_view(); // Deprecated
	std::vector<lt::torrent_handle>& get_torrent_handle_vector();
};

#endif
