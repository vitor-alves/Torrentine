SRC_PATH=./src
OUT_PATH=./bin
INCLUDE_PATH=./include
THIRDPARTY_PATH=./third_party
FILES = utility.cpp torrent.cpp config.cpp restAPI.cpp torrentManager.cpp torrentine.cpp ../third_party/cpp-base64/base64.cpp
CFLAGS= -std=c++14 -pthread -lboost_filesystem -lboost_system -ltorrent-rasterbar -lboost_program_options -lsqlite3 -lssl -lcrypto -lboost_iostreams -lcurl
CC = g++

all:
	${CC}  ${CFLAGS}  $(FILES:%.cpp=$(SRC_PATH)/%.cpp)  -I ${INCLUDE_PATH} -I ${THIRDPARTY_PATH} -o ${OUT_PATH}/torrentine

test:
	g++ -std=c++14 test/test.cpp -I ./include -I ./third_party -o ./bin/test

.PHONY: all test
