#include "torrent.h"

void Torrent::set_handle(lt::torrent_handle handle) {
	this->handle = handle;
}

lt::torrent_handle Torrent::get_handle() {
	return handle;
};
