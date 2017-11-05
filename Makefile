all:
	g++ utility.cpp torrent.cpp config.cpp restAPI.cpp torrentManager.cpp bitsleek.cpp -o bitsleek -pthread -lboost_filesystem -lboost_system -ltorrent-rasterbar -I lib/

script_test:
	g++ script.cpp -llua -o script_test

.PHONY: all script_test
