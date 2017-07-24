#include "lib/simple-web-server/server_http.hpp"
#include "torrentManager.h"

#ifndef REST_API_H
#define REST_API_H

typedef SimpleWeb::Server<SimpleWeb::HTTP> HttpServer;

class RestAPI {

private:
	HttpServer server;
	std::thread* server_thread;
	TorrentManager& torrent_manager;
	void define_resources();

public:
	RestAPI(int port, TorrentManager& torrentManager);
	~RestAPI();
	void start_server();
	void stop_server();
};

#endif