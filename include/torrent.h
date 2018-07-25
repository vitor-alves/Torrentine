#include <libtorrent/torrent_handle.hpp>
#include <boost/asio/ip/address.hpp>
#include <libtorrent/torrent_status.hpp>
#include <libtorrent/announce_entry.hpp>
#include <boost/optional.hpp>

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

	// TODO - those fields should be pointers to the properties, and not copies
	struct torrent_settings {
		boost::optional<int> upload_limit;
		boost::optional<int> download_limit;
		boost::optional<bool> sequential_download;
	};
	
private:
	lt::torrent_handle handle;
	unsigned long int const id; // TODO - ID should be uint64_t
public:
	torrent_settings get_torrent_settings();
	void set_torrent_settings(torrent_settings const ts);
	std::vector<torrent_file> get_torrent_files();	
	void set_handle(lt::torrent_handle handle);
	lt::torrent_handle &get_handle();
	unsigned long int const get_id();
	Torrent(unsigned long int const id);
	~Torrent();
	std::vector<Torrent::torrent_file> get_torrent_files(bool const piece_granularity);
	std::vector<Torrent::torrent_peer> get_torrent_peers();
	std::vector<lt::announce_entry> get_torrent_trackers();
	lt::torrent_status get_torrent_status();
	boost::shared_ptr<const lt::torrent_info> get_torrent_info();
};

#endif
