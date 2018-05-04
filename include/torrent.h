#include <libtorrent/torrent_handle.hpp>
#include <boost/asio/ip/address.hpp>
#include <libtorrent/torrent_status.hpp>

#ifndef TORRENT_H
#define TORRENT_H

namespace lt = libtorrent;

class Torrent {
public:
	// TODO - those fields should be pointers to the properties, and not copies
	struct torrent_peer {
		boost::asio::ip::tcp::endpoint ip;
		std::string client;
		int down_speed;
		int up_speed;
		boost::int64_t down_total;
		boost::int64_t up_total;
		float progress;
	};
	
	// TODO - those fields should be pointers to the properties, and not copies
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
	std::vector<Torrent::torrent_peer> get_torrent_peers();
	lt::torrent_status get_torrent_status();
};

#endif
