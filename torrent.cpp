#include "torrent.h"

void Torrent::set_handle(lt::torrent_handle handle) {
	this->handle = handle;
}

lt::torrent_handle Torrent::get_handle() {
	return handle;
}

Torrent::Torrent(const unsigned int id) {
	this->id = id;
	// TODO - confirm ID does not already exists
}

const unsigned int Torrent::get_id() {
	return id;
}

Torrent::~Torrent() {
}
