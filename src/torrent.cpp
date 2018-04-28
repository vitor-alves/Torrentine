#include "torrent.h"
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/peer_info.hpp>
#include "plog/Log.h"

void Torrent::set_handle(lt::torrent_handle handle) {
	this->handle = handle;
}

lt::torrent_handle &Torrent::get_handle() {
	return handle;
}

Torrent::Torrent(unsigned long int const id) : id(id) {
}

unsigned long int const Torrent::get_id() {
	return id;
}

std::vector<Torrent::torrent_file> Torrent::get_torrent_files(bool const piece_granularity) {
	std::vector<Torrent::torrent_file> torrent_files;
	
	
	//  If the torrent doesn't have metadata, the pointer will not be initialized (i.e. a NULL pointer).
	boost::shared_ptr<const lt::torrent_info> ti = handle.torrent_file();
	if(ti) {
		std::vector<boost::int64_t> progress;
		if(piece_granularity)
			handle.file_progress(progress, lt::torrent_handle::piece_granularity);	
		else
			handle.file_progress(progress);
		std::vector<int> priorities = handle.file_priorities();

		for(int i = 0; i < ti->num_files(); i++) {
			Torrent::torrent_file tf;
			tf.index = i; 
			tf.progress = progress.at(i);	
			tf.priority = priorities.at(i);
			tf.name = ti->files().file_name(i);
			tf.size = ti->files().file_size(i);
			tf.path = ti->files().file_path(i);
			torrent_files.push_back(tf);
		}
	}

	return torrent_files;
}

// TODO - the struct Torrent::torrent_peer is useless. Substitute it for libtorrent::torrent_peer. Use it instead. 
std::vector<Torrent::torrent_peer> Torrent::get_torrent_peers() {
	std::vector<Torrent::torrent_peer> torrent_peers;
	
	std::vector<lt::peer_info> peer_infos;
	try {
		handle.get_peer_info(peer_infos); // TODO - treat exceptions in ALL references to handle in other parts of the code. Read about invalid_handle exception here: https://www.libtorrent.org/reference-Core.html#torrent_handle 
	}
	catch(const lt::libtorrent_exception &e) {
		LOG_ERROR << "Could not get torrent peers info";
		return std::vector<Torrent::torrent_peer>();
	}

	for(lt::peer_info p : peer_infos) {
		Torrent::torrent_peer peer;
		peer.ip = p.ip;
		peer.client = p.client;
		peer.down_speed = p.down_speed;
		peer.up_speed = p.up_speed;
		peer.down_total = p.total_download;
		peer.up_total = p.total_upload;
		peer.progress = p.progress;
		torrent_peers.push_back(peer);	
	}

	return torrent_peers;
}

Torrent::~Torrent() {
}

lt::torrent_status Torrent::get_torrent_status() {
	
	lt::torrent_status status = handle.status();

	return status;
}
