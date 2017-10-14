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
	const unsigned int get_id();
	Torrent(const unsigned int id);
	~Torrent();
};

#endif
