#include "simple-web-server/server_http.hpp"
#include "simple-web-server/utility.hpp"
#include "torrentManager.h"
#include "config.h"
#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include <memory>

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
		boolean,
		int_number,
		double_number,
		text
	};

private:
	bool const enable_CORS = true; // TODO - This should be in configs
	HttpServer server;
	std::unique_ptr<std::thread> server_thread;
	TorrentManager& torrent_manager;
	ConfigManager& config;
	void define_resources();
	std::string torrent_file_path;
	std::string download_path;
	std::unordered_map<int, std::string> const error_codes = {{4150, "invalid Authorization. Access denied"},
								{4100, "invalid parameter in query string or missing required parameter"},
								{3100, "could not stop torrent"},
								{3110, "could not remove torrent"},
								{3120, "could not start torrent"},
								{3130, "could not recheck torrent"},
								{3140, "could not get torrent files"},
								{3150, "could not get log"},
								{3160, "could not get peers"},
								{3170, "could not get status"},
								{3180, "could not find boundary in request"},
								{3190, "request format is incorrect"},
								{3200, "internal server error"},
								{3210, "could not find filename field in header"},
								{3220, "no valid torrent file HTTP URL was found"},
								{3230, "server could not download torrent file from HTTP URL"},
								{3240, "there seems to be a problem with the provided torrent file"},
								{3250, "could not get trackers"},
								{3260, "could not get session status"}
	};
	bool validate_authorization(std::shared_ptr<HttpServer::Request> const request);
	std::string stringfy_document(rapidjson::Document const &document, bool const pretty=true);
	void respond_invalid_parameter(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> const request,
		       			std::string const parameter);
	void respond_invalid_authorization(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> const request);
	bool validate_parameter(SimpleWeb::CaseInsensitiveMultimap const &query, SimpleWeb::CaseInsensitiveMultimap::iterator const it_query, int const parameter_format, std::vector<std::string> const &allowed_values);
	std::string validate_all_parameters(SimpleWeb::CaseInsensitiveMultimap &query,
			std::map<std::string, api_parameter> &required_parameters,
			std::map<std::string, api_parameter> &optional_parameters);
	bool is_authorization_valid(std::string authorization_base64);
	bool is_parameter_format_valid(SimpleWeb::CaseInsensitiveMultimap::iterator const it_query, int const parameter_format);
	std::string decode_basic_auth(std::string authorization_base64);
	bool accepts_gzip_encoding(SimpleWeb::CaseInsensitiveMultimap &header);
public:
	RestAPI(ConfigManager &config, TorrentManager &torrent_manager);
	~RestAPI();
	void start_server();
	void stop_server();
	void torrents_stop(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);	
	void torrents_files_get(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);	
	void torrents_peers_get(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);
	void torrents_trackers_get(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);
	void torrents_recheck(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);	
	void torrents_start(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);	
	void torrents_status_get(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);
	void torrents_delete(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);
	void torrents_add(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);
	void session_status_get(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);
	void webUI_get(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);
	void get_logs(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);
	void add_torrents_from_request(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);
	int parse_request_to_atp(std::shared_ptr<HttpServer::Request> request, std::vector<lt::add_torrent_params> &parsed_atps);
};



#endif
