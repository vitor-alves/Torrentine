all:
	g++ utility.cpp torrent.cpp config.cpp restAPI.cpp torrentManager.cpp bitsleek.cpp -o bitsleek -pthread -lboost_filesystem -lboost_system -ltorrent-rasterbar -I lib/ && ./bitsleek
