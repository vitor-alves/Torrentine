all:
	g++ utility.cpp torrent.cpp config.cpp restAPI.cpp torrentManager.cpp bitsleek.cpp -o bitsleek -pthread -lboost_filesystem -lboost_system -ltorrent-rasterbar -I lib/

macro_test:
	g++ macro.cpp -llua -o macro_test

.PHONY: all macro_test
