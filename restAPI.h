#include "lib/simple-web-server/server_http.hpp"
#include "torrentManager.h"
#include "config.h"

#ifndef REST_API_H
#define REST_API_H

typedef SimpleWeb::Server<SimpleWeb::HTTP> HttpServer;

class RestAPI {

private:
	HttpServer server;
	std::thread* server_thread;
	TorrentManager& torrent_manager;
	void define_resources();
	std::string torrent_file_path;
	std::string download_path;

public:
	RestAPI(ConfigManager config, TorrentManager& torrentManager);
	~RestAPI();
	void start_server();
	void stop_server();	
};

#endif
