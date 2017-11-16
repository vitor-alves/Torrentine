#include "simple-web-server/server_http.hpp"
#include "torrentManager.h"
#include "config.h"
#include "lib/rapidjson/document.h"
#include "lib/rapidjson/prettywriter.h"
#include "lib/rapidjson/writer.h"
#include "lib/rapidjson/stringbuffer.h"

#ifndef REST_API_H
#define REST_API_H

typedef SimpleWeb::Server<SimpleWeb::HTTP> HttpServer;

using CaseInsensitiveMultimap = std::unordered_multimap<std::string, std::string, SimpleWeb::CaseInsensitiveHash, SimpleWeb::CaseInsensitiveEqual>;

class RestAPI {

private:
	struct api_parameter {
		std::string name;
		std::string value;
		int format;
		std::vector<std::string> allowed_values;	
	};

	enum api_parameter_format {
		boolean
	};

private:
	HttpServer server;
	std::thread* server_thread;
	TorrentManager& torrent_manager;
	void define_resources();
	std::string torrent_file_path;
	std::string download_path;
	std::unordered_map<int, std::string> const error_codes = {{4150, "invalid Authorization. Access denied"},
								{4100, "invalid parameter in query string or missing required parameter"},
								{3100, "could not stop torrent"}};
	bool validate_authorization(std::shared_ptr<HttpServer::Request> request);
	std::string stringfy_document(rapidjson::Document &document, bool pretty=true);
	void respond_invalid_parameter(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request,
		       			std::string const parameter);
	void respond_invalid_authorization(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);
	bool validate_parameter(SimpleWeb::CaseInsensitiveMultimap const &query, SimpleWeb::CaseInsensitiveMultimap::iterator const it_parameter, int const parameter_format, std::vector<std::string> const allowed_values);
	std::string validate_all_parameters(SimpleWeb::CaseInsensitiveMultimap &query,
			std::map<std::string, api_parameter> &required_parameters,
			std::map<std::string, api_parameter> &optional_parameters);
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
