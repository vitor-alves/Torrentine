#include <libtorrent/torrent_handle.hpp>

#ifndef TORRENT_H
#define TORRENT_H

namespace lt = libtorrent;

class Torrent {
public:
	struct torrent_file {
		int index;
		std::string name;
		boost::int64_t progress;
		boost::int64_t size;
		int priority;
		std::string path;
	};
	
private:
	lt::torrent_handle handle;
	unsigned long int const id;
public:
	std::vector<torrent_file> get_torrent_files();	
	void set_handle(lt::torrent_handle handle);
	lt::torrent_handle &get_handle();
	unsigned long int const get_id();
	Torrent(unsigned long int const id);
	~Torrent();
	std::vector<Torrent::torrent_file> get_torrent_files(bool const piece_granularity);
};

#endif
