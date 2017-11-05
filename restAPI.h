#include "simple-web-server/server_http.hpp"
#include "torrentManager.h"
#include "config.h"

#ifndef REST_API_H
#define REST_API_H

typedef SimpleWeb::Server<SimpleWeb::HTTP> HttpServer;

using CaseInsensitiveMultimap = std::unordered_multimap<std::string, std::string, SimpleWeb::CaseInsensitiveHash, SimpleWeb::CaseInsensitiveEqual>;

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
	void torrents_stop(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);	
	void torrents_get(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);
	void torrents_delete(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);
	void torrents_add(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);
	void webUI_get(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);
};

#endif
