#include "torrent.h"

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

Torrent::~Torrent() {
}
