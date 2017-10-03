all:
	g++ config.cpp restAPI.cpp torrentManager.cpp bitsleek.cpp -o bitsleek -pthread -lboost_filesystem -lboost_system -ltorrent-rasterbar && ./bitsleek
