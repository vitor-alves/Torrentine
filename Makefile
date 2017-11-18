SRC_PATH=./src
OUT_PATH=./bin
INCLUDE_PATH=./include
THIRDPARTY_PATH=./third_party
FILES = utility.cpp torrent.cpp config.cpp restAPI.cpp torrentManager.cpp bitsleek.cpp
CFLAGS= -std=c++14 -pthread -lboost_filesystem -lboost_system -ltorrent-rasterbar -lboost_program_options
CC = g++

all:
	${CC}  ${CFLAGS}  $(FILES:%.cpp=$(SRC_PATH)/%.cpp)  -I ${INCLUDE_PATH} -I ${THIRDPARTY_PATH} -o ${OUT_PATH}/bitsleek

script_test:
	g++ script.cpp -llua -o script_test

.PHONY: all script_test
