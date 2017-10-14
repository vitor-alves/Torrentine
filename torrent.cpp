#include "torrent.h"

void Torrent::set_handle(lt::torrent_handle handle) {
	this->handle = handle;
}

lt::torrent_handle Torrent::get_handle() {
	return handle;
}

Torrent::Torrent(unsigned int id) {
	this->id = id;
	// TODO - confirm ID does not already exists
}

unsigned int Torrent::get_id() {
	return id;
}
