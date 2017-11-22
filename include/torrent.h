#include <libtorrent/torrent_handle.hpp>

#ifndef TORRENT_H
#define TORRENT_H

namespace lt = libtorrent;

class Torrent {
private:
	lt::torrent_handle handle;
	unsigned long int const id;

public:
	void set_handle(lt::torrent_handle handle);
	lt::torrent_handle &get_handle();
	unsigned long int const get_id();
	Torrent(unsigned long int const id);
	~Torrent();
};

#endif
