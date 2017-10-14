#include <libtorrent/torrent_handle.hpp>

#ifndef TORRENT_H
#define TORRENT_H

namespace lt = libtorrent;

class Torrent {
private:
	lt::torrent_handle handle;
	unsigned int id;

public:
	void set_handle(lt::torrent_handle handle);
	lt::torrent_handle get_handle();
	unsigned int get_id();
	Torrent(unsigned int id);
};

#endif
