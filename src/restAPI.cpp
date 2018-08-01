#include <boost/filesystem.hpp>
#include <boost/range/iterator_range.hpp>
#include <fstream>
#include <vector>
#ifdef HAVE_OPENSSL
#include "crypto.hpp"
#endif
#include "restAPI.h"
#include "torrentManager.h"
#include "config.h"
#include "utility.h"
#include "plog/Log.h"
#include <typeinfo>
#include <vector>
#include <string>
#include <sqlite3.h>
#include "cpp-base64/base64.h"
#include "rapidjson/error/en.h"

RestAPI::RestAPI(ConfigManager &config, TorrentManager &torrent_manager) : torrent_manager(torrent_manager), config(config) {
	try {
		torrent_file_path = config.get_config<std::string>("directory.torrent_file_path");
		download_path = config.get_config<std::string>("directory.download_path"); 
		server.config.port = config.get_config<unsigned short>("api.port");
		server.config.address = config.get_config<std::string>("api.address");	
	}
	catch(config_key_error const &e) {
		LOG_ERROR << "Problem initializing RestAPI. Could not get config: " << e.what();
	}

	define_resources();
}		

RestAPI::~RestAPI() {
	stop_server();
}

void RestAPI::start_server() {
	server_thread = std::make_unique<std::thread>( [&]() { 
			try {
			LOG_INFO << "Web UI and REST API server will start listening on "
			<< server.config.address << ":" << server.config.port;
			server.start();
			}
			catch(std::exception const &e) {
			LOG_ERROR << "Web server error: " << e.what() << ". Is port " <<
			server.config.port << " already in use?" ;
			std::cerr << "Web server error: " << e.what() << ". Is port " <<
			server.config.port << " already in use?"
			<< " Check log files for more information." << std::endl;
			}} );

	server.on_error = [](std::shared_ptr<HttpServer::Request> request, const SimpleWeb::error_code & ec) {
		LOG_ERROR << "Web server error: " << ec.message();
	};
}

void RestAPI::stop_server() {
	server.stop();
	if(server_thread->joinable()) {
		server_thread->join();
		LOG_DEBUG << "REST API server thread has been joined";
	}
}

// WARNING: do not add or remove resources after start() is called
void RestAPI::define_resources() {
	/* Allow CORS (Cross Origin Request) - OPTIONS
	 * https://stackoverflow.com/questions/29954037/why-is-an-options-request-sent-and-can-i-disable-it
	 * https://husobee.github.io/golang/cors/2015/09/26/cors.html */
	/* Allow CORS (Cross Origin Request) - OPTIONS */
	server.resource["^.*$"]["OPTIONS"] =
		[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) 
		{ 
			std::string resp;
			std::string http_header;
			std::string http_status;
		
			std::string origin_str;
			std::string request_headers_str;
			std::string request_methods_str;
			std::string credentials_str = "true";
			if(enable_CORS) {
				resp = "CORS is enabled";

				auto header = request->header;
				auto origin = header.find("Origin");
				if(origin != header.end()) {
					origin_str = origin->second;
				}

				auto request_headers = header.find("Access-Control-Request-Headers");
				if(request_headers != header.end()) {
					request_headers_str = request_headers->second;
				}
				
				auto request_methods = header.find("Access-Control-Request-Method");
				if(request_methods != header.end()) {
					request_methods_str = request_methods->second;
				}

				http_header += "Access-Control-Allow-Origin: " + origin_str + "\r\n";
				http_header += "Access-Control-Allow-Credentials: " + credentials_str + "\r\n";
				http_header += "Access-Control-Allow-Methods: " + request_methods_str  + "\r\n";
				http_header += "Access-Control-Allow-Headers: " + request_headers_str  + "\r\n";
			}
			else {
				resp = "CORS is disabled";
			}
		
			http_header += "Content-Length: " + std::to_string(resp.length()) + "\r\n";
			http_status = "200 OK";

			LOG_DEBUG << "HTTP " << request->method << " " << request->path << " "  << http_status
				<< " to " << request->remote_endpoint_address();

			*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n" << resp;
		};

	/* /torrents/<id*>/stop - PATCH */
	server.resource["^/v1.0/torrents/(?:([0-9,]*)/|)stop$"]["PATCH"] =
		[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) 
		{ this->torrents_stop(response, request); };

	/* /torrents/<id*>/files - GET */
	server.resource["^/v1.0/torrents/(?:([0-9,]*)/|)files$"]["GET"] =
		[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) 
		{ this->torrents_files_get(response, request); };

	/* /torrents/<id*>/peers - GET */
	server.resource["^/v1.0/torrents/(?:([0-9,]*)/|)peers$"]["GET"] =
		[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) 
		{ this->torrents_peers_get(response, request); };

	/* /torrents/<id*>/recheck - POST */
	server.resource["^/v1.0/torrents/(?:([0-9,]*)/|)recheck$"]["PATCH"] =
		[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) 
		{ this->torrents_recheck(response, request); };

	/* /torrents/<id*>/start - PATCH */
	server.resource["^/v1.0/torrents/(?:([0-9,]*)/|)start$"]["PATCH"] =
		[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) 
		{ this->torrents_start(response, request); };

	/* /torrents/<id*>/status - GET */
	server.resource["^/v1.0/torrents/(?:([0-9,]*)/|)status$"]["GET"] =
		[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) 
		{ this->torrents_status_get(response, request); };

	/* /torrents/<id*> - DELETE */
	server.resource["^/v1.0/torrents(?:/([0-9,]+)|)$"]["DELETE"] = /* TODO - I am not sure if this regex is working when multiple ids provided.
											I make a request to delete 1,2,3 and it did not work. Test it. */
		[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request)
		{ this->torrents_delete(response, request); };

	/* /logs - GET */
	server.resource["^/v1.0/logs$"]["GET"] =
		[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request)
		{ this->get_logs(response, request); };

	/* /torrents - POST */
	server.resource["^/v1.0/torrents$"]["POST"] =
		[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) 
		{ this->torrents_add(response, request); };

	/* /torrents/upload - POST */
	server.resource["^/v1.0/torrents/upload$"]["POST"] =
		[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) 
		{ this->torrents_upload_files(response, request); };

	/* /torrents/<id*>/trakers - GET */
	server.resource["^/v1.0/torrents/(?:([0-9,]*)/|)trackers$"]["GET"] =
		[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) 
		{ this->torrents_trackers_get(response, request); };

	/* /program/status - GET */
	server.resource["^/v1.0/program/status$"]["GET"] =
		[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) 
		{ this->program_status_get(response, request); };

	/* /program/settings - GET */
	server.resource["^/v1.0/program/settings$"]["GET"] =
		[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) 
		{ this->program_settings_get(response, request); };

	/* /torrents/<id*>/info - GET */
	server.resource["^/v1.0/torrents/(?:([0-9,]*)/|)info$"]["GET"] =
		[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) 
		{ this->torrents_info_get(response, request); };

	/* /torrents/<id*>/settings - GET */
	server.resource["^/v1.0/torrents/(?:([0-9,]*)/|)settings$"]["GET"] =
		[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) 
		{ this->torrents_settings_get(response, request); };

	/* /torrents/settings - PATCH */
	server.resource["^/v1.0/torrents/settings$"]["PATCH"] =
		[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) 
		{ this->torrents_settings_set(response, request); };

	/* /program/settings - PATCH */
	server.resource["^/v1.0/program/settings$"]["PATCH"] =
		[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) 
		{ this->program_settings_set(response, request); };

	/* /queue/torrents/<id> - PATCH */
	server.resource["^/v1.0/queue/torrents(?:/([0-9]+))$"]["PATCH"] =
		[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) 
		{ this->queue_torrents_set(response, request); };

	/* /authorization - GET */
	server.resource["^/v1.0/authorization$"]["GET"] =
		[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request)
		{ this->get_authorization(response, request); };

	/* /filesystem/directory - GET */
	server.resource["^/v1.0/filesystem/directory$"]["GET"] =
		[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) 
		{ this->server_directory_get(response, request); };

	/* /stream - GET */
	server.resource["^/v1.0/stream$"]["GET"] =
		[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) 
		{ this->stream_get(response, request); };

	/* / - GET WEB UI*/
	server.default_resource["GET"] =
		[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request)
		{ this->webUI_get(response, request); };
}

std::string RestAPI::validate_all_parameters(SimpleWeb::CaseInsensitiveMultimap &query,
		std::map<std::string, api_parameter> &required_parameters,
		std::map<std::string, api_parameter> &optional_parameters) {

	SimpleWeb::CaseInsensitiveMultimap::iterator it_query;
	for(auto p = required_parameters.begin(); p != required_parameters.end(); p++) {
		it_query = query.find(p->second.name);
		if(it_query != query.end()) {
			if(validate_parameter(query, it_query, p->second.format, p->second.allowed_values)) {
				p->second.value = it_query->second;
			}
			else {
				return p->second.name;
			}
		}
		else {
			return p->second.name;
		}
	}

	for(auto p = optional_parameters.begin(); p != optional_parameters.end(); p++) {
		it_query = query.find(p->second.name);
		if(it_query != query.end()) {
			if(validate_parameter(query, it_query, p->second.format, p->second.allowed_values)) {
				p->second.value = it_query->second;
			}
			else {
				return p->second.name;
			}
		}
	}
	return "";
}

bool RestAPI::accepts_gzip_encoding(SimpleWeb::CaseInsensitiveMultimap &header) {
	auto accept_encoding = header.find("Accept-Encoding");
	if(accept_encoding != header.end()) {
		std::vector<std::string> encodings = split_string(accept_encoding->second, ',');
		for(std::string i : encodings) {
			if(i == "gzip") {
				return true;
			}
		}
	}
	return false;
}

void RestAPI::torrents_recheck(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
	if(!validate_authorization(request)) {
		respond_invalid_authorization(response, request);
		return;
	}

	std::vector<unsigned long int> ids = split_string_to_ulong(request->path_match[1], ',');
	unsigned long int result = torrent_manager.recheck_torrents(ids);

	std::string http_header;
	std::string origin_str;
	std::string credentials_str = "true";
	if(enable_CORS) {
		auto header = request->header;
		
		auto origin = header.find("Origin");
		if(origin != header.end()) {
			origin_str = origin->second;
		}


		http_header += "Access-Control-Allow-Origin: " + origin_str + "\r\n";
		http_header += "Access-Control-Allow-Credentials: " + credentials_str + "\r\n";
	}

	rapidjson::Document document;
	document.SetObject();
	rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
	std::string http_status;
	std::stringstream ss_response;
	if(result == 0) {
		char const *message = "The torrents will be rechecked";
		document.AddMember("message", rapidjson::StringRef(message), allocator);

		std::string json = stringfy_document(document);	

		if(accepts_gzip_encoding(request->header)) {
			ss_response << gzip_encode(json);
			http_header += "Content-Encoding: gzip\r\n";
		}
		else {
			ss_response << json;
		}
		http_header += "Content-Length: " + std::to_string(ss_response.str().length()) + "\r\n";
		http_header += "Content-Type: application/json\r\n";
		http_status = "200 OK";

		LOG_DEBUG << "HTTP " << request->method << " " << request->path << " "  << http_status
			<< " to " << request->remote_endpoint_address() << " Message: " << message;

		*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n" << ss_response.str();
	}
	else {
		rapidjson::Value errors(rapidjson::kArrayType);
		rapidjson::Value e(rapidjson::kObjectType);
		e.AddMember("code", 3130, allocator);
		char const *message = error_codes.find(3130)->second.c_str();
		e.AddMember("message", rapidjson::StringRef(message), allocator);
		e.AddMember("id", result, allocator);
		errors.PushBack(e, allocator);
		document.AddMember("errors", errors, allocator);

		std::string json = stringfy_document(document);

		if(accepts_gzip_encoding(request->header)) {
			ss_response << gzip_encode(json);
			http_header += "Content-Encoding: gzip\r\n";
		}
		else {
			ss_response << json;
		}

		http_header += "Content-Length: " + std::to_string(ss_response.str().length()) + "\r\n";
		http_header += "Content-Type: application/json\r\n";
		http_status = "404 Not Found";

		LOG_DEBUG << "HTTP " << request->method << " " << request->path << " "  << http_status
			<< " to " << request->remote_endpoint_address() << " Message: " << message;

		*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n" << ss_response.str();
	}
}

void RestAPI::torrents_stop(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
	if(!validate_authorization(request)) {
		respond_invalid_authorization(response, request);
		return;
	}

	std::map<std::string, api_parameter> required_parameters = {};
	std::map<std::string, api_parameter> optional_parameters = {
		{"force_stop",{"force_stop","false",api_parameter_format::boolean,{"true","false"}}} };
	SimpleWeb::CaseInsensitiveMultimap query = request->parse_query_string();
	std::string invalid_parameter = validate_all_parameters(query, required_parameters, optional_parameters);
	if(invalid_parameter.length() > 0) { 
		respond_invalid_parameter(response, request, invalid_parameter);
		return;
	}

	std::vector<unsigned long int> ids = split_string_to_ulong(request->path_match[1], ',');
	unsigned long int result = torrent_manager.stop_torrents(ids, str_to_bool(optional_parameters.find("force_stop")->second.value));

	std::string http_header;
	std::string origin_str;
	std::string credentials_str = "true";
	if(enable_CORS) {
		auto header = request->header;
		
		auto origin = header.find("Origin");
		if(origin != header.end()) {
			origin_str = origin->second;
		}

		http_header += "Access-Control-Allow-Origin: " + origin_str + "\r\n";
		http_header += "Access-Control-Allow-Credentials: " + credentials_str + "\r\n";
	}

	rapidjson::Document document;
	document.SetObject();
	rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
	std::string http_status;
	std::stringstream ss_response;
	if(result == 0) {
		char const *message = "An attempt to stop the torrents will be made asynchronously";
		document.AddMember("message", rapidjson::StringRef(message), allocator);

		std::string json = stringfy_document(document);	

		if(accepts_gzip_encoding(request->header)) {
			ss_response << gzip_encode(json);
			http_header += "Content-Encoding: gzip\r\n";
		}
		else {
			ss_response << json;
		}
		http_header += "Content-Length: " + std::to_string(ss_response.str().length()) + "\r\n";
		http_header += "Content-Type: application/json\r\n";
		http_status = "202 Accepted";

		LOG_DEBUG << "HTTP " << request->method << " " << request->path << " "  << http_status
			<< " to " << request->remote_endpoint_address() << " Message: " << message;

		*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n" << ss_response.str();
	}
	else {
		rapidjson::Value errors(rapidjson::kArrayType);
		rapidjson::Value e(rapidjson::kObjectType);
		e.AddMember("code", 3100, allocator);
		char const *message = error_codes.find(3100)->second.c_str();
		e.AddMember("message", rapidjson::StringRef(message), allocator);
		e.AddMember("id", result, allocator);
		errors.PushBack(e, allocator);
		document.AddMember("errors", errors, allocator);

		std::string json = stringfy_document(document);

		if(accepts_gzip_encoding(request->header)) {
			ss_response << gzip_encode(json);
			http_header += "Content-Encoding: gzip\r\n";
		}
		else {
			ss_response << json;
		}

		http_header += "Content-Length: " + std::to_string(ss_response.str().length()) + "\r\n";
		http_header += "Content-Type: application/json\r\n";
		http_status = "404 Not Found";

		LOG_DEBUG << "HTTP " << request->method << " " << request->path << " "  << http_status
			<< " to " << request->remote_endpoint_address() << " Message: " << message;

		*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n" << ss_response.str();
	}
}

void RestAPI::get_logs(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
	if(!validate_authorization(request)) {
		respond_invalid_authorization(response, request);
		return;
	}

	fs::path log_path;
	try {
		log_path = fs::path(config.get_config<std::string>("log.file_path"));
	}
	catch(config_key_error const &e) {
		LOG_ERROR << "Could not get log. Could not get config: " << e.what();
	}

	std::vector<char> buffer;
	bool result = file_to_buffer(buffer, log_path.string());

	std::string http_header;
	std::string origin_str;
	std::string credentials_str = "true";
	if(enable_CORS) {
		auto header = request->header;
		
		auto origin = header.find("Origin");
		if(origin != header.end()) {
			origin_str = origin->second;
		}

		http_header += "Access-Control-Allow-Origin: " + origin_str + "\r\n";
		http_header += "Access-Control-Allow-Credentials: " + credentials_str + "\r\n";
	}

	rapidjson::Document document;
	document.SetObject();
	rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
	std::string http_status;
	std::stringstream ss_response;
	if(result == true) {
		char const *message = "Successfully sent log";

		std::string response_file(&buffer[0]);

		if(accepts_gzip_encoding(request->header)) {
			ss_response << gzip_encode(response_file);
			http_header += "Content-Encoding: gzip\r\n";
		}
		else {
			ss_response << response_file;
		}
		http_header += "Content-Length: " + std::to_string(ss_response.str().length()) + "\r\n";
		http_header += "Content-Type: text/plain\r\n";
		http_header += "Content-Disposition: inline; filename=torrentine-log.txt\r\n";
		http_status = "200 OK";

		LOG_DEBUG << "HTTP " << request->method << " " << request->path << " "  << http_status
			<< " to " << request->remote_endpoint_address() << " Message: " << message;

		*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n" << ss_response.str();
	}
	else {
		rapidjson::Value errors(rapidjson::kArrayType);
		rapidjson::Value e(rapidjson::kObjectType);
		e.AddMember("code", 3150, allocator);
		char const *message = error_codes.find(3150)->second.c_str();
		e.AddMember("message", rapidjson::StringRef(message), allocator);
		errors.PushBack(e, allocator);
		document.AddMember("errors", errors, allocator);

		std::string json = stringfy_document(document);

		if(accepts_gzip_encoding(request->header)) {
			ss_response << gzip_encode(json);
			http_header += "Content-Encoding: gzip\r\n";
		}
		else {
			ss_response << json;
		}

		http_header += "Content-Length: " + std::to_string(ss_response.str().length()) + "\r\n";
		http_header += "Content-Type: application/json\r\n";
		http_status = "404 Not Found";

		LOG_DEBUG << "HTTP " << request->method << " " << request->path << " "  << http_status
			<< " to " << request->remote_endpoint_address() << " Message: " << message;

		*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n" << ss_response.str();
	}
}

void RestAPI::torrents_peers_get(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
	if(!validate_authorization(request)) {
		respond_invalid_authorization(response, request);
		return;
	}

	std::vector<std::vector<Torrent::torrent_peer>> requested_torrent_peers;
	std::vector<unsigned long int> ids = split_string_to_ulong(request->path_match[1], ',');
	if(ids.size() == 0) // If no ids were specified, consider all ids
		ids = torrent_manager.get_all_ids(); 
	unsigned long int result = torrent_manager.get_peers_torrents(requested_torrent_peers, ids);

	std::string http_header;
	std::string origin_str;
	std::string credentials_str = "true";
	if(enable_CORS) {
		auto header = request->header;
		
		auto origin = header.find("Origin");
		if(origin != header.end()) {
			origin_str = origin->second;
		}

		http_header += "Access-Control-Allow-Origin: " + origin_str + "\r\n";
		http_header += "Access-Control-Allow-Credentials: " + credentials_str + "\r\n";
	}

	rapidjson::Document document;
	document.SetObject();
	rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
	std::string http_status;
	std::stringstream ss_response;
	if(result == 0) {
		char const *message = "Succesfuly retrieved torrent peers";
		document.AddMember("message", rapidjson::StringRef(message), allocator);
		rapidjson::Value torrents(rapidjson::kObjectType);
		std::vector<unsigned long int>::iterator it_ids= ids.begin();	
		for(std::vector<Torrent::torrent_peer> torrent_peers : requested_torrent_peers) {
			rapidjson::Value t(rapidjson::kObjectType);
			rapidjson::Value peers(rapidjson::kArrayType);
			rapidjson::Value temp_value;
			for(Torrent::torrent_peer tp : torrent_peers) {
				rapidjson::Value f(rapidjson::kObjectType);
				rapidjson::Value client;
				client.SetString(tp.client.c_str(), tp.client.size(), allocator);
				rapidjson::Value address;
				address.SetString(tp.ip.address().to_string().c_str(), tp.ip.address().to_string().size(), allocator);
				f.AddMember("ip", address, allocator);
				f.AddMember("port", tp.ip.port(), allocator);
				f.AddMember("client", client, allocator);
				f.AddMember("down_speed", tp.down_speed, allocator);
				f.AddMember("up_speed", tp.up_speed, allocator);
				f.AddMember("down_total", tp.down_total, allocator);
				f.AddMember("up_total", tp.up_total, allocator);
				f.AddMember("progress", tp.progress, allocator);
				peers.PushBack(f, allocator);
			}
			t.AddMember("peers", peers, allocator);
			std::string temp_id = std::to_string(*it_ids);
			it_ids++;
			temp_value.SetString(temp_id.c_str(), temp_id.length(), allocator);
			torrents.AddMember(temp_value, t, allocator);	
		}
		document.AddMember("torrents", torrents, allocator);

		std::string json = stringfy_document(document);	

		if(accepts_gzip_encoding(request->header)) {
			ss_response << gzip_encode(json);
			http_header += "Content-Encoding: gzip\r\n";
		}
		else {
			ss_response << json;
		}
		http_header += "Content-Length: " + std::to_string(ss_response.str().length()) + "\r\n";
		http_header += "Content-Type: application/json\r\n";
		http_status = "200 OK";

		LOG_DEBUG << "HTTP " << request->method << " " << request->path << " "  << http_status
			<< " to " << request->remote_endpoint_address() << " Message: " << message;

		*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n" << ss_response.str();
	}
	else {
		rapidjson::Value errors(rapidjson::kArrayType);
		rapidjson::Value e(rapidjson::kObjectType);
		e.AddMember("code", 3160, allocator);
		char const *message = error_codes.find(3160)->second.c_str();
		e.AddMember("message", rapidjson::StringRef(message), allocator);
		e.AddMember("id", result, allocator);
		errors.PushBack(e, allocator);
		document.AddMember("errors", errors, allocator);

		std::string json = stringfy_document(document);

		if(accepts_gzip_encoding(request->header)) {
			ss_response << gzip_encode(json);
			http_header += "Content-Encoding: gzip\r\n";
		}
		else {
			ss_response << json;
		}

		http_header += "Content-Length: " + std::to_string(ss_response.str().length()) + "\r\n";
		http_header += "Content-Type: application/json\r\n";
		http_status = "404 Not Found";

		LOG_DEBUG << "HTTP " << request->method << " " << request->path << " "  << http_status
			<< " to " << request->remote_endpoint_address() << " Message: " << message;

		*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n" << ss_response.str();
	}
}

void RestAPI::torrents_files_get(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
	if(!validate_authorization(request)) {
		respond_invalid_authorization(response, request);
		return;
	}

	std::map<std::string, api_parameter> required_parameters = {};
	std::map<std::string, api_parameter> optional_parameters = {
		{"piece_granularity",{"piece_granularity","true",api_parameter_format::boolean,{"true","false"}}} };
	SimpleWeb::CaseInsensitiveMultimap query = request->parse_query_string();
	std::string invalid_parameter = validate_all_parameters(query, required_parameters, optional_parameters);
	if(invalid_parameter.length() > 0) { 
		respond_invalid_parameter(response, request, invalid_parameter);
		return;
	}

	std::vector<std::vector<Torrent::torrent_file>> requested_torrent_files;
	std::vector<unsigned long int> ids = split_string_to_ulong(request->path_match[1], ',');
	if(ids.size() == 0) // If no ids were specified, consider all ids
		ids = torrent_manager.get_all_ids(); // TODO - use this same approach in all other API calls and reduce the redundant code in the action methods. This way we do not need to treat ids empty differently than ids non empty in the action method, cuz its always non empty (if torrents exist). 
	unsigned long int result = torrent_manager.get_files_torrents(requested_torrent_files, ids, str_to_bool(optional_parameters.find("piece_granularity")->second.value));

	std::string http_header;
	std::string origin_str;
	std::string credentials_str = "true";
	if(enable_CORS) {
		auto header = request->header;
		
		auto origin = header.find("Origin");
		if(origin != header.end()) {
			origin_str = origin->second;
		}

		http_header += "Access-Control-Allow-Origin: " + origin_str + "\r\n";
		http_header += "Access-Control-Allow-Credentials: " + credentials_str + "\r\n";
	}

	rapidjson::Document document;
	document.SetObject();
	rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
	std::string http_status;
	std::stringstream ss_response;
	if(result == 0) {
		char const *message = "Succesfuly retrieved torrent files";
		document.AddMember("message", rapidjson::StringRef(message), allocator);
		rapidjson::Value torrents(rapidjson::kObjectType);
		std::vector<unsigned long int>::iterator it_ids= ids.begin();	
		for(std::vector<Torrent::torrent_file> torrent_files : requested_torrent_files) {
			rapidjson::Value t(rapidjson::kObjectType);
			rapidjson::Value files(rapidjson::kObjectType);
			rapidjson::Value temp_value;
			for(Torrent::torrent_file tf : torrent_files) {
				rapidjson::Value f(rapidjson::kObjectType);
				rapidjson::Value name;
				name.SetString(tf.name.c_str(), tf.name.size(), allocator);
				// TODO - this works, but I do not know if this is sufficient to implement the UI file tree view in JS easily.
				// Maybe I will need to tweak this later. Deluge also sends a field "type" that specifies if its a dir or a file.
				// Not sure if I need that, but keep that in mind.	
				f.AddMember("name", name, allocator);
				f.AddMember("progress", double(tf.progress)/double(tf.size), allocator);
				f.AddMember("downloaded_total", tf.progress, allocator);
				f.AddMember("size", tf.size, allocator);
				f.AddMember("priority", tf.priority, allocator);
				rapidjson::Value path;
				path.SetString(tf.path.c_str(), tf.path.size(), allocator);
				f.AddMember("path", path, allocator);
				//f.AddMember("index", tf.index, allocator);
				std::string temp_index = std::to_string(tf.index);
				temp_value.SetString(temp_index.c_str(), temp_index.length(), allocator);
				files.AddMember(temp_value, f, allocator);
			}
			t.AddMember("files", files, allocator);
			std::string temp_id = std::to_string(*it_ids);
			it_ids++;
			temp_value.SetString(temp_id.c_str(), temp_id.length(), allocator);
			torrents.AddMember(temp_value, t, allocator);	
		}
		document.AddMember("torrents", torrents, allocator);

		std::string json = stringfy_document(document);	

		if(accepts_gzip_encoding(request->header)) {
			ss_response << gzip_encode(json);
			http_header += "Content-Encoding: gzip\r\n";
		}
		else {
			ss_response << json;
		}
		http_header += "Content-Length: " + std::to_string(ss_response.str().length()) + "\r\n";
		http_header += "Content-Type: application/json\r\n";
		http_status = "200 OK";

		LOG_DEBUG << "HTTP " << request->method << " " << request->path << " "  << http_status
			<< " to " << request->remote_endpoint_address() << " Message: " << message;

		*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n" << ss_response.str();
	}
	else {
		rapidjson::Value errors(rapidjson::kArrayType);
		rapidjson::Value e(rapidjson::kObjectType);
		e.AddMember("code", 3140, allocator);
		char const *message = error_codes.find(3140)->second.c_str();
		e.AddMember("message", rapidjson::StringRef(message), allocator);
		e.AddMember("id", result, allocator);
		errors.PushBack(e, allocator);
		document.AddMember("errors", errors, allocator);

		std::string json = stringfy_document(document);

		if(accepts_gzip_encoding(request->header)) {
			ss_response << gzip_encode(json);
			http_header += "Content-Encoding: gzip\r\n";
		}
		else {
			ss_response << json;
		}

		http_header += "Content-Length: " + std::to_string(ss_response.str().length()) + "\r\n";
		http_header += "Content-Type: application/json\r\n";
		http_status = "404 Not Found";

		LOG_DEBUG << "HTTP " << request->method << " " << request->path << " "  << http_status
			<< " to " << request->remote_endpoint_address() << " Message: " << message;

		*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n" << ss_response.str();
	}
}

void RestAPI::torrents_start(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
	if(!validate_authorization(request)) {
		respond_invalid_authorization(response, request);
		return;
	}

	std::vector<unsigned long int> ids = split_string_to_ulong(request->path_match[1], ',');
	unsigned long int result = torrent_manager.start_torrents(ids);
			
	std::string http_header;
	std::string origin_str;
	std::string credentials_str = "true";
	if(enable_CORS) {
		auto header = request->header;
		
		auto origin = header.find("Origin");
		if(origin != header.end()) {
			origin_str = origin->second;
		}

		http_header += "Access-Control-Allow-Origin: " + origin_str + "\r\n";
		http_header += "Access-Control-Allow-Credentials: " + credentials_str + "\r\n";
	}

	rapidjson::Document document;
	document.SetObject();
	rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
	std::string http_status;
	std::stringstream ss_response;
	if(result == 0) {
		char const *message = "An attempt to start the torrents will be made asynchronously";
		document.AddMember("message", rapidjson::StringRef(message), allocator);

		std::string json = stringfy_document(document);	

		if(accepts_gzip_encoding(request->header)) {
			ss_response << gzip_encode(json);
			http_header += "Content-Encoding: gzip\r\n";
		}
		else {
			ss_response << json;
		}
		http_header += "Content-Length: " + std::to_string(ss_response.str().length()) + "\r\n";
		http_header += "Content-Type: application/json\r\n";
		http_status = "202 Accepted";

		LOG_DEBUG << "HTTP " << request->method << " " << request->path << " "  << http_status
			<< " to " << request->remote_endpoint_address() << " Message: " << message;

		*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n" << ss_response.str();
	}
	else {
		rapidjson::Value errors(rapidjson::kArrayType);
		rapidjson::Value e(rapidjson::kObjectType);
		e.AddMember("code", 3120, allocator);
		char const *message = error_codes.find(3120)->second.c_str();
		e.AddMember("message", rapidjson::StringRef(message), allocator);
		e.AddMember("id", result, allocator);
		errors.PushBack(e, allocator);
		document.AddMember("errors", errors, allocator);

		std::string json = stringfy_document(document);

		if(accepts_gzip_encoding(request->header)) {
			ss_response << gzip_encode(json);
			http_header += "Content-Encoding: gzip\r\n";
		}
		else {
			ss_response << json;
		}

		http_header += "Content-Length: " + std::to_string(ss_response.str().length()) + "\r\n";
		http_header += "Content-Type: application/json\r\n";
		http_status = "404 Not Found";

		LOG_DEBUG << "HTTP " << request->method << " " << request->path << " "  << http_status
			<< " to " << request->remote_endpoint_address() << " Message: " << message;

		*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n" << ss_response.str();
	}
}

// TODO - some improvements are needed to make the json fields more readable.
// 	- tests needed to check if everything is working fine.
// 	TODO - lt::error_code errc, error_file, error_file_exception etc curently not used. Use this to inform the user of possible errors in the torrent. All other variables are used, except the ones regarding to errors. No errors are treated at the moment.
void RestAPI::torrents_status_get(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
	if(!validate_authorization(request)) {
		respond_invalid_authorization(response, request);
		return;
	}

	std::vector<unsigned long int> ids = split_string_to_ulong(request->path_match[1], ',');
	// TODO - Inefficient. When ids.size() = 0 should be treated inside the calls, not here.
	if(ids.size() == 0) // If no ids were specified, consider all ids
		ids = torrent_manager.get_all_ids(); 
	std::vector<lt::torrent_status> torrents_status;
	unsigned long int result = torrent_manager.get_torrents_status(torrents_status, ids);

	std::string http_header;
	std::string origin_str;
	std::string credentials_str = "true";
	if(enable_CORS) {
		auto header = request->header;
		
		auto origin = header.find("Origin");
		if(origin != header.end()) {
			origin_str = origin->second;
		}

		http_header += "Access-Control-Allow-Origin: " + origin_str + "\r\n";
		http_header += "Access-Control-Allow-Credentials: " + credentials_str + "\r\n";
	}

	rapidjson::Document document;
	document.SetObject();
	rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
	std::string http_status;
	std::stringstream ss_response;
	if(result == 0) {
		char const *message = "Succesfuly retrieved torrent status";
		document.AddMember("message", rapidjson::StringRef(message), allocator);
		rapidjson::Value torrents(rapidjson::kObjectType);
		std::vector<unsigned long int>::iterator it_ids= ids.begin();	
		for(lt::torrent_status status : torrents_status) {
			lt::torrent_handle handle = status.handle;
			rapidjson::Value t(rapidjson::kObjectType);
			//t.AddMember("id", *it_ids, allocator);
			//it_ids++;
			rapidjson::Value s(rapidjson::kObjectType);
			rapidjson::Value temp_value;
			temp_value.SetString(status.name.c_str(), status.name.length(), allocator);
			s.AddMember("name", temp_value, allocator);
			s.AddMember("download_rate", status.download_rate, allocator);
			s.AddMember("download_limit", handle.download_limit(), allocator);
			s.AddMember("upload_rate", status.upload_rate, allocator); 
			s.AddMember("upload_limit", handle.upload_limit(), allocator);
			s.AddMember("progress", status.progress, allocator); 
			s.AddMember("total_download", status.total_download, allocator);
			s.AddMember("total_payload_download", status.total_payload_download, allocator);
			s.AddMember("download_payload_rate", status.download_payload_rate, allocator);
			s.AddMember("total_payload_upload", status.total_payload_upload, allocator);
			s.AddMember("upload_payload_rate", status.upload_payload_rate, allocator);
			s.AddMember("total_failed_bytes", status.total_failed_bytes, allocator);
			s.AddMember("total_redundant_bytes", status.total_redundant_bytes, allocator);
			s.AddMember("total_done", status.total_done, allocator);
			s.AddMember("total_upload", status.total_upload, allocator); 
			s.AddMember("num_seeds", status.num_seeds, allocator);
			temp_value.SetString(status.save_path.c_str(), status.save_path.length(), allocator);
			s.AddMember("save_path", temp_value, allocator);
			s.AddMember("next_announce", lt::duration_cast<lt::seconds>(status.next_announce).count(), allocator);
			temp_value.SetString(status.current_tracker.c_str(), status.current_tracker.length(), allocator);
			s.AddMember("current_tracker", temp_value, allocator);
			s.AddMember("num_peers", status.num_peers, allocator);
			s.AddMember("total_wanted_done", status.total_wanted_done, allocator);
			s.AddMember("total_wanted", status.total_wanted, allocator);
			s.AddMember("all_time_upload", status.all_time_upload, allocator);
			s.AddMember("all_time_download", status.all_time_download, allocator);
			s.AddMember("added_time", status.added_time, allocator);
			s.AddMember("completed_time", status.completed_time, allocator);
			s.AddMember("last_seen_complete", status.last_seen_complete, allocator);
			s.AddMember("storage_mode", status.storage_mode, allocator);
			s.AddMember("progress_ppm", status.progress_ppm, allocator);
			s.AddMember("queue_position", status.queue_position, allocator);
			s.AddMember("num_complete", status.num_complete, allocator);
			s.AddMember("num_incomplete", status.num_incomplete, allocator);
			s.AddMember("list_seeds", status.list_seeds, allocator);
			s.AddMember("list_peers", status.list_peers, allocator);
			s.AddMember("connect_candidates", status.connect_candidates, allocator);
			s.AddMember("num_pieces", status.num_pieces, allocator);
			s.AddMember("distributed_full_copies", status.distributed_full_copies, allocator);
			s.AddMember("distributed_fraction", status.distributed_fraction, allocator);
			s.AddMember("distributed_copies", status.distributed_copies, allocator);
			s.AddMember("block_size", status.block_size, allocator);
			s.AddMember("num_uploads", status.num_uploads, allocator);
			s.AddMember("num_connections", status.num_connections, allocator);
			s.AddMember("uploads_limit", status.uploads_limit, allocator);
			s.AddMember("connections_limit", status.connections_limit, allocator);
			s.AddMember("up_bandwidth_queue", status.up_bandwidth_queue, allocator);
			s.AddMember("down_bandwidth_queue", status.down_bandwidth_queue, allocator); 
			s.AddMember("seed_rank", status.seed_rank, allocator); 
			s.AddMember("checking_resume_data", status.checking_resume_data, allocator); 
			s.AddMember("need_save_resume", status.need_save_resume, allocator); 
			s.AddMember("is_seeding", status.is_seeding, allocator); 
			s.AddMember("is_finished", status.is_finished, allocator); 
			s.AddMember("has_metadata", status.has_metadata, allocator); 
			s.AddMember("has_incoming", status.has_incoming, allocator); 
			s.AddMember("moving_storage", status.moving_storage, allocator); 
			s.AddMember("announcing_to_trackers", status.announcing_to_trackers, allocator); 
			s.AddMember("announcing_to_lsd", status.announcing_to_lsd, allocator); 
			s.AddMember("announcing_to_dht", status.announcing_to_dht, allocator);
			// TODO - deprecated in 1.2
			// use last_upload, last_download or
			// seeding_duration, finished_duration and active_duration
			// instead
			s.AddMember("time_since_upload", status.time_since_upload, allocator);  // TODO
			s.AddMember("time_since_download", status.time_since_download, allocator); 
			s.AddMember("active_time", status.active_time, allocator); 
			s.AddMember("finished_time", status.finished_time, allocator); 
			s.AddMember("seeding_time", status.seeding_time, allocator); 
			/* info_hash: If this handle is to a torrent that hasn't loaded yet (for instance by being added) by a URL,
			   the returned value is undefined. */
			std::stringstream ss_info_hash;
			ss_info_hash << status.info_hash;
			std::string info_hash = ss_info_hash.str();
			temp_value.SetString(info_hash.c_str(), info_hash.length(), allocator);
			s.AddMember("info_hash", temp_value, allocator);	
			t.AddMember("status", s, allocator);
			std::string temp_id = std::to_string(*it_ids);
			temp_value.SetString(temp_id.c_str(), temp_id.length(), allocator);
			it_ids++;
			torrents.AddMember(temp_value, t, allocator);
		}
		document.AddMember("torrents", torrents, allocator);

		std::string json = stringfy_document(document);	

		if(accepts_gzip_encoding(request->header)) {
			ss_response << gzip_encode(json);
			http_header += "Content-Encoding: gzip\r\n";
		}
		else {
			ss_response << json;
		}
		
		http_header += "Content-Length: " + std::to_string(ss_response.str().length()) + "\r\n";
		http_header += "Content-Type: application/json\r\n";
		http_status = "200 OK";

		LOG_DEBUG << "HTTP " << request->method << " " << request->path << " "  << http_status
			<< " to " << request->remote_endpoint_address() << " Message: " << message;

		*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n" << ss_response.str();
	}
	else {
		rapidjson::Value errors(rapidjson::kArrayType);
		rapidjson::Value e(rapidjson::kObjectType);
		e.AddMember("code", 3170, allocator);
		char const *message = error_codes.find(3170)->second.c_str();
		e.AddMember("message", rapidjson::StringRef(message), allocator);
		e.AddMember("id", result, allocator);
		errors.PushBack(e, allocator);
		document.AddMember("errors", errors, allocator);

		std::string json = stringfy_document(document);

		if(accepts_gzip_encoding(request->header)) {
			ss_response << gzip_encode(json);
			http_header += "Content-Encoding: gzip\r\n";
		}
		else {
			ss_response << json;
		}

		http_header += "Content-Length: " + std::to_string(ss_response.str().length()) + "\r\n";
		http_header += "Content-Type: application/json\r\n";
		http_status = "404 Not Found";

		LOG_DEBUG << "HTTP " << request->method << " " << request->path << " "  << http_status
			<< " to " << request->remote_endpoint_address() << " Message: " << message;

		*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n" << ss_response.str();
	}
}

void RestAPI::torrents_delete(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {	
	if(!validate_authorization(request)) {
		respond_invalid_authorization(response, request);
		return;
	}

	std::map<std::string, api_parameter> required_parameters = {};
	std::map<std::string, api_parameter> optional_parameters = {
		{"remove_data",{"remove_data","false",api_parameter_format::boolean,{"true","false"}}} };
	SimpleWeb::CaseInsensitiveMultimap query = request->parse_query_string();
	std::string invalid_parameter = validate_all_parameters(query, required_parameters, optional_parameters);
	if(invalid_parameter.length() > 0) { 
		respond_invalid_parameter(response, request, invalid_parameter);
		return;
	}

	std::vector<unsigned long int> ids = split_string_to_ulong(request->path_match[1], ',');

	unsigned long int result = torrent_manager.remove_torrent(ids, str_to_bool(optional_parameters.find("remove_data")->second.value));

	std::string http_header;
	std::string origin_str;
	std::string credentials_str = "true";
	if(enable_CORS) {
		auto header = request->header;
		
		auto origin = header.find("Origin");
		if(origin != header.end()) {
			origin_str = origin->second;
		}

		http_header += "Access-Control-Allow-Origin: " + origin_str + "\r\n";
		http_header += "Access-Control-Allow-Credentials: " + credentials_str + "\r\n";
	}

	rapidjson::Document document;
	document.SetObject();
	rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
	std::string http_status;
	std::stringstream ss_response;
	if(result == 0) {
		char const *message = "An attempt to remove the torrents will be made asynchronously";
		document.AddMember("message", rapidjson::StringRef(message), allocator);

		std::string json = stringfy_document(document);	

		if(accepts_gzip_encoding(request->header)) {
			ss_response << gzip_encode(json);
			http_header += "Content-Encoding: gzip\r\n";
		}
		else {
			ss_response << json;
		}
		http_header += "Content-Length: " + std::to_string(ss_response.str().length()) + "\r\n";
		http_header += "Content-Type: application/json\r\n";
		http_status = "202 Accepted";

		LOG_DEBUG << "HTTP " << request->method << " " << request->path << " "  << http_status
			<< " to " << request->remote_endpoint_address() << " Message: " << message;

		*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n" << ss_response.str();
	}
	else {
		rapidjson::Value errors(rapidjson::kArrayType);
		rapidjson::Value e(rapidjson::kObjectType);
		e.AddMember("code", 3110, allocator);
		char const *message = error_codes.find(3110)->second.c_str();
		e.AddMember("message", rapidjson::StringRef(message), allocator);
		e.AddMember("id", result, allocator);
		errors.PushBack(e, allocator);
		document.AddMember("errors", errors, allocator);

		std::string json = stringfy_document(document);

		if(accepts_gzip_encoding(request->header)) {
			ss_response << gzip_encode(json);
			http_header += "Content-Encoding: gzip\r\n";
		}
		else {
			ss_response << json;
		}

		http_header += "Content-Length: " + std::to_string(ss_response.str().length()) + "\r\n";
		http_header += "Content-Type: application/json\r\n";
		http_status = "404 Not Found";

		LOG_DEBUG << "HTTP " << request->method << " " << request->path << " "  << http_status
			<< " to " << request->remote_endpoint_address() << " Message: " << message;

		*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n" << ss_response.str();
	}
}

int RestAPI::parse_request_to_atp(std::shared_ptr<HttpServer::Request> request, std::vector<lt::add_torrent_params> &parsed_atps) {
	int error_code = 0;	
	std::string buffer;
	buffer.resize(131072);

	std::string boundary;
	if(!getline(request->content, boundary)) {
		error_code = 3180;
		LOG_ERROR << error_codes.at(error_code); 
		return error_code;
	}

	// go through all content parts
	while(true) {
		std::stringstream content;
		std::string filename;
		std::string name;

		auto header = SimpleWeb::HttpHeader::parse(request->content);
		auto header_it = header.find("Content-Disposition");
		if(header_it != header.end()) {
			auto content_disposition_attributes = SimpleWeb::HttpHeader::FieldValue::SemicolonSeparatedAttributes::parse(header_it->second);
			auto name_it = content_disposition_attributes.find("name");
			if(name_it != content_disposition_attributes.end()) {
				name = name_it->second;

				bool add_newline_next = false; // there is an extra newline before content boundary, this avoids adding this extra newline to file
				while(true) {
					request->content.getline(&buffer[0], static_cast<std::streamsize>(buffer.size()));
					if(request->content.eof()) {
						error_code = 3190;
						LOG_ERROR << error_codes.at(error_code);
						return error_code;
					}
					auto size = request->content.gcount();

					if(size >= 2 && (static_cast<size_t>(size - 1) == boundary.size() || static_cast<size_t>(size - 1) == boundary.size() + 2) && // last boundary ends with: --
							std::strncmp(buffer.c_str(), boundary.c_str(), boundary.size() - 1 /*ignore \r*/) == 0 &&
							buffer[static_cast<size_t>(size) - 2] == '\r') // buffer must also include \r at end
						break;

					if(add_newline_next) {
						content.put('\n');
						add_newline_next = false;
					}

					if(!request->content.fail()) { // got line or section that ended with newline
						content.write(buffer.c_str(), size - 1); // size includes newline character, but buffer does not
						add_newline_next = true;
					}
					else
						content.write(buffer.c_str(), size);

					request->content.clear(); // clear stream state
				}
				// TODO - there should be a verification to check if torrentfile is not above X megabytes (to avoid abuse)	
				if(name == "torrentfile") {
					auto filename_it = content_disposition_attributes.find("filename");
					if(filename_it != content_disposition_attributes.end()) {
						filename = filename_it->second;

						fs::path const received_torrent = torrent_file_path + filename;

						std::ofstream ofs(received_torrent.string());
						ofs << content.rdbuf();
						ofs.close();

						std::vector<char> buffer;
						if(!file_to_buffer(buffer, received_torrent.string())) {
							LOG_ERROR << "A problem occured while adding torrent with filename " << received_torrent.string();
							error_code = 3200;
							return error_code;	
						}

						lt::bdecode_node node;
						char const *buf = buffer.data();	
						lt::error_code ec;
						int ret =  lt::bdecode(buf, buf+buffer.size(), node, ec);
						if(ec) {
							LOG_ERROR << "Problem occured while decoding torrent buffer: " << ec.message();
							error_code = 3240;
							return error_code;	
						}	

						lt::add_torrent_params atp;
						lt::torrent_info info(node);	
						boost::shared_ptr<lt::torrent_info> t_info = boost::make_shared<lt::torrent_info>(info);
						atp.ti = t_info;
						atp.save_path = download_path;

						parsed_atps.push_back(atp);
					}
					else {
						error_code = 3210; 
						LOG_ERROR << error_codes.at(error_code);
						return error_code;
					}
				}
				else if(name == "magnet") {
					lt::add_torrent_params atp;
					atp.url = content.str();
					atp.save_path = download_path;

					parsed_atps.push_back(atp);
				}
				else if(name == "infohash") {
					lt::add_torrent_params atp;

					std::string infohash_str = content.str();
					lt::sha1_hash hash;
					content >> hash;
					atp.info_hash = hash;
					atp.save_path = download_path;

					parsed_atps.push_back(atp);
				}
				else if(name == "http") {
					std::string url = content.str();
					if(!url.empty() && url[url.size()-1] == '\r' ) { // TODO - Messing with \r, \n, \r\n may cause issues in different platforms... Test this heavily.
						url.erase(url.size()-1);
					}
					std::vector<std::string> splitted_url = split_string(url, '/');

					if(splitted_url.empty()) {
						error_code = 3220;
						LOG_ERROR << error_codes.at(error_code);
						return error_code;
					}
					std::string filename = splitted_url.at(splitted_url.size()-1);
					if(filename.empty()) {
						error_code = 3220;
						LOG_ERROR << error_codes.at(error_code);
						return error_code;
					}

					std::string out_file = torrent_file_path + filename;
					// TODO - this download is being done sincronously and slows down the HTTP response considerably. Make the process of downloading the .torrent and adding in async somehow when the adding method is "http"
					bool success = download_file(url.c_str(), out_file.c_str()); 
					if(success == false) {
						// TODO - this needs to be improved. When the file cannot be downloaded the server responds that the file cannot be downloaded but does not specify which file cant be downloaded (does not respond the url. only logs it). I think its time to create an error object to organize things. I am not happy with how the errors are being treated in cases like this because its confusing. Find a good solution. 
						LOG_ERROR << "Could not download file " + url;
						error_code = 3230;
						return error_code;
					}

					std::vector<char> buffer;
					if(!file_to_buffer(buffer, torrent_file_path + filename)) {
						LOG_ERROR << "Could not open file" + filename;
						error_code = 3200;
						return error_code;
					}

					lt::bdecode_node node;
					char const *buf = buffer.data();	
					lt::error_code ec;
					int ret =  lt::bdecode(buf, buf+buffer.size(), node, ec);
					if(ec) {
						LOG_ERROR << "Problem occured while decoding torrent: " + ec.message(); 
						error_code = 3240;
						return error_code;
					}	

					lt::add_torrent_params atp;
					lt::torrent_info info(node);	
					boost::shared_ptr<lt::torrent_info> t_info = boost::make_shared<lt::torrent_info>(info);
					atp.ti = t_info;
					atp.save_path = download_path;

					parsed_atps.push_back(atp);
				}
				else {
					LOG_ERROR << "Unknown name \"" << name << "\" in form data was ignored";
				}
			}
		}
		else { // no more parts
			error_code = 0;
			return error_code;
		}
	}

}

int RestAPI::save_request_files_to_disk(std::shared_ptr<HttpServer::Request> request, std::vector<std::string> &saved_torrents_path) {
	int error_code = 0;	
	std::string buffer;
	buffer.resize(131072);

	std::string boundary;
	if(!getline(request->content, boundary)) {
		error_code = 3180;
		LOG_ERROR << error_codes.at(error_code); 
		return error_code;
	}

	// go through all content parts
	while(true) {
		std::stringstream content;
		std::string filename;
		std::string name;

		auto header = SimpleWeb::HttpHeader::parse(request->content);
		auto header_it = header.find("Content-Disposition");
		if(header_it != header.end()) {
			auto content_disposition_attributes = SimpleWeb::HttpHeader::FieldValue::SemicolonSeparatedAttributes::parse(header_it->second);
			auto name_it = content_disposition_attributes.find("name");
			if(name_it != content_disposition_attributes.end()) {
				name = name_it->second;

				bool add_newline_next = false; // there is an extra newline before content boundary, this avoids adding this extra newline to file
				while(true) {
					request->content.getline(&buffer[0], static_cast<std::streamsize>(buffer.size()));
					if(request->content.eof()) {
						error_code = 3190;
						LOG_ERROR << error_codes.at(error_code);
						return error_code;
					}
					auto size = request->content.gcount();

					if(size >= 2 && (static_cast<size_t>(size - 1) == boundary.size() || static_cast<size_t>(size - 1) == boundary.size() + 2) && // last boundary ends with: --
							std::strncmp(buffer.c_str(), boundary.c_str(), boundary.size() - 1 /*ignore \r*/) == 0 &&
							buffer[static_cast<size_t>(size) - 2] == '\r') // buffer must also include \r at end
						break;

					if(add_newline_next) {
						content.put('\n');
						add_newline_next = false;
					}

					if(!request->content.fail()) { // got line or section that ended with newline
						content.write(buffer.c_str(), size - 1); // size includes newline character, but buffer does not
						add_newline_next = true;
					}
					else
						content.write(buffer.c_str(), size);

					request->content.clear(); // clear stream state
				}
				// TODO - there should be a verification to check if torrentfile is not above X megabytes (to avoid abuse)	
				if(name == "file") { // TODO - check also for the following headers in the request: content-type must be from bittorrent 
					filename = random_string(20) + ".torrent"; 

					fs::path const received_torrent = torrent_file_path + filename;

					std::ofstream ofs(received_torrent.string());
					ofs << content.rdbuf();
					ofs.close();

					saved_torrents_path.push_back(filename);
				}
			}
		}
		else { // no more parts
			error_code = 0;
			return error_code;
		}
	}

}

void RestAPI::torrents_upload_files(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
	if(!validate_authorization(request)) {
	   respond_invalid_authorization(response, request);
	   return;
	}

	std::vector<std::string> saved_torrents_path;
	int error_code = save_request_files_to_disk(request, saved_torrents_path);

	if(error_code != 0) {
		 // TODO - else respond / log error
	}
	std::string http_header;
	std::string origin_str;
	std::string credentials_str = "true";
	if(enable_CORS) {
		auto header = request->header;
		
		auto origin = header.find("Origin");
		if(origin != header.end()) {
			origin_str = origin->second;
		}

		http_header += "Access-Control-Allow-Origin: " + origin_str + "\r\n";
		http_header += "Access-Control-Allow-Credentials: " + credentials_str + "\r\n";
	}

	rapidjson::Document document;
	document.SetObject();
	rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
	std::string http_status;
	std::stringstream ss_response;
	if(error_code == 0) {
		char const *message = "Torrents uploaded successfully";
		document.AddMember("message", rapidjson::StringRef(message), allocator);
		rapidjson::Value t(rapidjson::kArrayType);
		for(std::string path : saved_torrents_path) {
			rapidjson::Value s(rapidjson::kObjectType);
			rapidjson::Value temp_value;
			temp_value.SetString(path.c_str(), path.length(), allocator);
			s.AddMember("path", temp_value, allocator);
			t.PushBack(s, allocator);
		}
		document.AddMember("uploaded_torrents", t, allocator);

		std::string json = stringfy_document(document);	

		if(accepts_gzip_encoding(request->header)) {
			ss_response << gzip_encode(json);
			http_header += "Content-Encoding: gzip\r\n";
		}
		else {
			ss_response << json;
		}
		http_header += "Content-Length: " + std::to_string(ss_response.str().length()) + "\r\n";
		http_header += "Content-Type: application/json\r\n";
		http_status = "200 OK";

		LOG_DEBUG << "HTTP " << request->method << " " << request->path << " "  << http_status
			<< " to " << request->remote_endpoint_address() << " Message: " << message;

		*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n" << ss_response.str();
	}
	/* TODO - Another design limitation with error codes here. The http_status should be determined by the error code. For example, sometimes we need 500 and sometimes we need 400 and the error code should contain that information. Error codes NEED TO be an object of some kind to add make this happen.*/
	else {
		rapidjson::Value errors(rapidjson::kArrayType);
		rapidjson::Value e(rapidjson::kObjectType);
		e.AddMember("code", error_code, allocator);
		char const *message = error_codes.find(error_code)->second.c_str();
		e.AddMember("message", rapidjson::StringRef(message), allocator);
		errors.PushBack(e, allocator);
		document.AddMember("errors", errors, allocator);

		std::string json = stringfy_document(document);

		if(accepts_gzip_encoding(request->header)) {
			ss_response << gzip_encode(json);
			http_header += "Content-Encoding: gzip\r\n";
		}
		else {
			ss_response << json;
		}

		http_header += "Content-Length: " + std::to_string(ss_response.str().length()) + "\r\n";
		http_header += "Content-Type: application/json\r\n";
		http_status = "404 Not Found";

		LOG_DEBUG << "HTTP " << request->method << " " << request->path << " "  << http_status
			<< " to " << request->remote_endpoint_address() << " Message: " << message;

		*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n" << ss_response.str();
	}
}

void RestAPI::torrents_add(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
	if(!validate_authorization(request)) {
	   respond_invalid_authorization(response, request);
	   return;
	}

	std::vector<lt::add_torrent_params> parsed_atps;
	int error_code = parse_request_to_atp(request, parsed_atps);

	if(error_code == 0) {
		for(lt::add_torrent_params atp : parsed_atps) {
			torrent_manager.add_torrent_async(atp);
		}
	} // TODO - else respond error

	std::string http_header;
	std::string origin_str;
	std::string credentials_str = "true";
	if(enable_CORS) {
		auto header = request->header;
		
		auto origin = header.find("Origin");
		if(origin != header.end()) {
			origin_str = origin->second;
		}

		http_header += "Access-Control-Allow-Origin: " + origin_str + "\r\n";
		http_header += "Access-Control-Allow-Credentials: " + credentials_str + "\r\n";
	}

	rapidjson::Document document;
	document.SetObject();
	rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
	std::string http_status;
	std::stringstream ss_response;
	if(error_code == 0) {
		char const *message = "An attempt to add the torrents will be made asynchronously";
		document.AddMember("message", rapidjson::StringRef(message), allocator);

		std::string json = stringfy_document(document);	

		if(accepts_gzip_encoding(request->header)) {
			ss_response << gzip_encode(json);
			http_header += "Content-Encoding: gzip\r\n";
		}
		else {
			ss_response << json;
		}
		http_header += "Content-Length: " + std::to_string(ss_response.str().length()) + "\r\n";
		http_header += "Content-Type: application/json\r\n";
		http_status = "202 Accepted";

		LOG_DEBUG << "HTTP " << request->method << " " << request->path << " "  << http_status
			<< " to " << request->remote_endpoint_address() << " Message: " << message;

		*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n" << ss_response.str();
	}
	/* TODO - Another design limitation with error codes here. The http_status should be determined by the error code. For example, sometimes we need 500 and sometimes we need 400 and the error code should contain that information. Error codes NEED TO be an object of some kind to add make this happen.*/
	else {
		rapidjson::Value errors(rapidjson::kArrayType);
		rapidjson::Value e(rapidjson::kObjectType);
		e.AddMember("code", error_code, allocator);
		char const *message = error_codes.find(error_code)->second.c_str();
		e.AddMember("message", rapidjson::StringRef(message), allocator);
		errors.PushBack(e, allocator);
		document.AddMember("errors", errors, allocator);

		std::string json = stringfy_document(document);

		if(accepts_gzip_encoding(request->header)) {
			ss_response << gzip_encode(json);
			http_header += "Content-Encoding: gzip\r\n";
		}
		else {
			ss_response << json;
		}

		http_header += "Content-Length: " + std::to_string(ss_response.str().length()) + "\r\n";
		http_header += "Content-Type: application/json\r\n";
		http_status = "404 Not Found";

		LOG_DEBUG << "HTTP " << request->method << " " << request->path << " "  << http_status
			<< " to " << request->remote_endpoint_address() << " Message: " << message;

		*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n" << ss_response.str();
	}
}

void RestAPI::webUI_get(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
	try {
		auto web_root_path = boost::filesystem::canonical("webUI");
		auto path = boost::filesystem::canonical(web_root_path / request->path);
		// Check if path is within web_root_path
		if(std::distance(web_root_path.begin(), web_root_path.end()) > std::distance(path.begin(), path.end()) ||
				!std::equal(web_root_path.begin(), web_root_path.end(), path.begin()))
			throw std::invalid_argument("path must be within root path");
		if(boost::filesystem::is_directory(path))
			path /= "index.html";

		SimpleWeb::CaseInsensitiveMultimap header;

		//    Uncomment the following line to enable Cache-Control
		//    header.emplace("Cache-Control", "max-age=86400");

#ifdef HAVE_OPENSSL
		//    Uncomment the following lines to enable ETag
		//    {
		//      ifstream ifs(path.string(), ifstream::in | ios::binary);
		//      if(ifs) {
		//        auto hash = SimpleWeb::Crypto::to_hex_string(SimpleWeb::Crypto::md5(ifs));
		//        header.emplace("ETag", "\"" + hash + "\"");
		//        auto it = request->header.find("If-None-Match");
		//        if(it != request->header.end()) {
		//          if(!it->second.empty() && it->second.compare(1, hash.size(), hash) == 0) {
		//            response->write(SimpleWeb::StatusCode::redirection_not_modified, header);
		//            return;
		//          }
		//        }
		//      }
		//      else
		//        throw invalid_argument("could not read file");
		//    }
#endif

		auto ifs = std::make_shared<std::ifstream>();
		ifs->open(path.string(), std::ifstream::in | std::ios::binary | std::ios::ate);

		if(*ifs) {
			auto length = ifs->tellg();
			ifs->seekg(0, std::ios::beg);

			header.emplace("Content-Length", std::to_string(length));
			response->write(header);

			// Trick to define a recursive function within this scope (for your convenience)
			class FileServer {
				public:
					static void read_and_send(const std::shared_ptr<HttpServer::Response> &response, const std::shared_ptr<std::ifstream> &ifs) {
						// Read and send 128 KB at a time
						static std::vector<char> buffer(131072); // Safe when server is running on one thread
						std::streamsize read_length;
						if((read_length = ifs->read(&buffer[0], buffer.size()).gcount()) > 0) {
							response->write(&buffer[0], read_length);
							if(read_length == static_cast<std::streamsize>(buffer.size())) {
								response->send([response, ifs](const SimpleWeb::error_code &ec) {
										if(!ec)
										read_and_send(response, ifs);
										else
										std::cerr << "Connection interrupted" << std::endl; // TODO - log
										});
							}
						}
					}
			};
			FileServer::read_and_send(response, ifs);
		}
		else
			throw std::invalid_argument("could not read file");
	}
	catch(const std::exception &e) {
		response->write(SimpleWeb::StatusCode::client_error_bad_request, "Could not open path " + request->path + ": " + e.what());
		LOG_ERROR << "Could not open path " + request->path + ": " + e.what();
	}
}

bool RestAPI::validate_authorization(std::shared_ptr<HttpServer::Request> const request) {
	SimpleWeb::CaseInsensitiveMultimap header = request->header;
	auto authorization = header.find("Authorization");
	if(authorization != header.end() && is_authorization_valid(authorization->second)) {
		return true;
	}
	else {
		return false;
	}
}

void RestAPI::respond_invalid_authorization(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> const request) {
	rapidjson::Document document;
	document.SetObject();
	rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
	rapidjson::Value errors(rapidjson::kArrayType);
	rapidjson::Value e(rapidjson::kObjectType);
	e.AddMember("code", 4150, allocator);
	char const *message = error_codes.find(4150)->second.c_str();
	e.AddMember("message", rapidjson::StringRef(message), allocator);
	errors.PushBack(e, allocator);
	document.AddMember("errors", errors, allocator);

	std::string json = stringfy_document(document);

	std::string http_status = "401 Unauthorized";
	std::string http_header;
	std::string origin_str;
	std::string credentials_str = "true";
	if(enable_CORS) {
		auto header = request->header;
		
		auto origin = header.find("Origin");
		if(origin != header.end()) {
			origin_str = origin->second;
		}

		http_header += "Access-Control-Allow-Origin: " + origin_str + "\r\n";
		http_header += "Access-Control-Allow-Credentials: " + credentials_str + "\r\n";
	}

	http_header += "Content-Length: " + std::to_string(json.length()) + "\r\n";

	*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n" << json;

	LOG_DEBUG << "HTTP " << request->method << " " << request->path << " "  << http_status
		<< " to " << request->remote_endpoint_address() << " Message: " << message;
}

std::string RestAPI::stringfy_document(rapidjson::Document const &document, bool const pretty) {
	rapidjson::StringBuffer string_buffer;
	std::string json;

	if(pretty) {
		rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(string_buffer);
		document.Accept(writer);
		json = string_buffer.GetString();
	} 
	else {
		rapidjson::Writer<rapidjson::StringBuffer> writer(string_buffer);
		document.Accept(writer);
		json = string_buffer.GetString();
	}
	return json;
}


void RestAPI::respond_invalid_parameter(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> const request, std::string const parameter) {
	rapidjson::Document document;
	document.SetObject();
	rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
	rapidjson::Value errors(rapidjson::kArrayType);
	rapidjson::Value e(rapidjson::kObjectType);
	e.AddMember("code", 4100, allocator);
	char const *message = error_codes.find(4100)->second.c_str();
	e.AddMember("message", rapidjson::StringRef(message), allocator);
	e.AddMember("parameter", rapidjson::StringRef(parameter.c_str()), allocator);
	errors.PushBack(e, allocator);
	document.AddMember("errors", errors, allocator);

	std::string json = stringfy_document(document);

	std::string http_status = "400 Bad Request";

	std::string http_header;
	std::string origin_str;
	std::string credentials_str = "true";
	if(enable_CORS) {
		auto header = request->header;
		
		auto origin = header.find("Origin");
		if(origin != header.end()) {
			origin_str = origin->second;
		}

		http_header += "Access-Control-Allow-Origin: " + origin_str + "\r\n";
		http_header += "Access-Control-Allow-Credentials: " + credentials_str + "\r\n";
	}

	http_header += "Content-Length: " + std::to_string(json.length()) + "\r\n";

	LOG_DEBUG << "HTTP " << request->method << " " << request->path << " "  << http_status
		<< " to " << request->remote_endpoint_address() << " Message: " << message;

	*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n" << json;

}

bool RestAPI::is_parameter_format_valid(SimpleWeb::CaseInsensitiveMultimap::iterator const it_query, int const parameter_format) {
	if(is_text_boolean(it_query->second) && parameter_format == api_parameter_format::boolean) {
		return true;
	}
	else if(is_text_int_number(it_query->second) && parameter_format == api_parameter_format::int_number) {
		return true;
	}
	else if(is_text_double_number(it_query->second) && parameter_format == api_parameter_format::double_number) {
		return true;
	}
	else if(it_query->second.size() > 0 && parameter_format == api_parameter_format::text) {
		return true;
	}
	else {
		return false;
	}
}

bool RestAPI::validate_parameter(SimpleWeb::CaseInsensitiveMultimap const &query, SimpleWeb::CaseInsensitiveMultimap::iterator const it_query, int const parameter_format, std::vector<std::string> const &allowed_values) {
	if(it_query == query.end()) {
		return false;
	}
	if(!is_parameter_format_valid(it_query, parameter_format)) {
		return false;
	}
	if(allowed_values.empty()) {
		return true;
	}
	for(std::string value : allowed_values) {
		if(it_query->second == value) {
			return true;
		}
	}
	return false;
}

std::string RestAPI::decode_basic_auth(std::string authorization_base64) {
	std::stringstream ss(authorization_base64);
	std::string s;
	char delim = ' ';

	getline(ss, s, delim);
	if(s != "Basic") {
		return std::string("");
	}

	getline(ss, s, delim);
	return base64_decode(s);
}

bool RestAPI::is_authorization_valid(std::string authorization_base64) {
	sqlite3 *db;
	sqlite3_stmt *stmt;

	std::string authorization = decode_basic_auth(authorization_base64);
	if(authorization.size() == 0) {
		return false;
	}
	std::vector<std::string> user_pass = split_string(authorization, ':');
	if(user_pass.size() != 2) {
		return false;
	}
	std::string username = user_pass.at(0);
	std::string password = user_pass.at(1);

	std::stringstream sql;
	sql << "select id,username,password,salt from users where username = ?";

	fs::path users_db_path;
	try {
		users_db_path = fs::path(config.get_config<std::string>("directory.database_path"));
	}
	catch(config_key_error const &e) {
		LOG_ERROR << "Could not find database config. Authorization for user " << username << " denied." << " Could not get config: " << e.what();
		return false;
	}

	if(sqlite3_open(users_db_path.string().c_str(), &db) != SQLITE_OK) {
		LOG_ERROR << "Could not open database " << users_db_path.string() << ". SQLite3 error_msg: " << sqlite3_errmsg(db); 
		sqlite3_close(db);
		return false;
	}

	if(sqlite3_prepare_v2(db, sql.str().c_str(), -1, &stmt, NULL) != SQLITE_OK) {
		LOG_ERROR << "Could not compile SQL: " << sql.str() << ". SQLite3 error_msg: " << sqlite3_errmsg(db); 
		sqlite3_close(db);
		sqlite3_finalize(stmt);
		return false;
	}

	int ret_code = 0;
	bool found = false;
	int id;
	std::string username_db;
	std::string password_db;
	std::string salt_db;
	sqlite3_bind_text(stmt, 1, username.c_str(), username.size(), SQLITE_STATIC);
	while((ret_code = sqlite3_step(stmt)) == SQLITE_ROW) {
		id = sqlite3_column_int(stmt, 0);
		username_db = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
		password_db = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
		salt_db = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
		found = true;
	}

	if(!found) {
		LOG_DEBUG << "Authorization for user " << username << " denied." << " User not found in database.";
		sqlite3_finalize(stmt);
		sqlite3_close(db);
		return false;
	}

	if(ret_code != SQLITE_DONE) {
		LOG_ERROR << "Problem while evaluating SQL: " << sql.str() << ". SQLite3 error_msg: " << sqlite3_errmsg(db);
	}

	sqlite3_finalize(stmt);
	sqlite3_close(db);

	std::string password_hash = generate_password_hash(password.c_str(), (unsigned char*)salt_db.c_str());
	if(password_hash == password_db) {
		LOG_DEBUG << "Authorization for user " << username << " allowed.";
		return true;
	}
	else {
		LOG_DEBUG << "Authorization for user " << username << " denied." << " Wrong password.";
		return false;
	}	
}

void RestAPI::torrents_trackers_get(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
	if(!validate_authorization(request)) {
		respond_invalid_authorization(response, request);
		return;
	}

	std::vector<std::vector<lt::announce_entry>> requested_torrent_trackers;
	std::vector<unsigned long int> ids = split_string_to_ulong(request->path_match[1], ',');
	if(ids.size() == 0) // If no ids were specified, consider all ids
		ids = torrent_manager.get_all_ids(); // TODO - use this same approach in all other API calls and reduce the redundant code in the action methods. This way we do not need to treat ids empty differently than ids non empty in the action method, cuz its always non empty (if torrents exist). 
	unsigned long int result = torrent_manager.get_trackers_torrents(requested_torrent_trackers, ids);

	std::string http_header;
	std::string origin_str;
	std::string credentials_str = "true";
	if(enable_CORS) {
		auto header = request->header;
		
		auto origin = header.find("Origin");
		if(origin != header.end()) {
			origin_str = origin->second;
		}

		http_header += "Access-Control-Allow-Origin: " + origin_str + "\r\n";
		http_header += "Access-Control-Allow-Credentials: " + credentials_str + "\r\n";
	}

	rapidjson::Document document;
	document.SetObject();
	rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
	std::string http_status;
	std::stringstream ss_response;
	// TODO - change the name of the variables here. I copied it from elsewhere
	if(result == 0) {
		char const *message = "Succesfuly retrieved torrent trackers";
		document.AddMember("message", rapidjson::StringRef(message), allocator);
		rapidjson::Value torrents(rapidjson::kObjectType);
		std::vector<unsigned long int>::iterator it_ids= ids.begin();	
		for(std::vector<lt::announce_entry> torrent_trackers : requested_torrent_trackers) {
			rapidjson::Value t(rapidjson::kObjectType);
			rapidjson::Value files(rapidjson::kArrayType);
			rapidjson::Value temp_value;
			for(lt::announce_entry tf : torrent_trackers) {
				rapidjson::Value f(rapidjson::kObjectType);
				f.AddMember("tier", tf.tier, allocator);
				temp_value.SetString(tf.url.c_str(), tf.url.length(), allocator);
				f.AddMember("url", temp_value, allocator);
				f.AddMember("next_announce", lt::duration_cast<lt::seconds>(tf.next_announce - std::chrono::system_clock::now()).count(), allocator);
				std::string is_working;
				if(tf.is_working())
					is_working = "true";
				else
					is_working = "false";
				temp_value.SetString(is_working.c_str(), is_working.length(), allocator);
				f.AddMember("is_working", temp_value, allocator);
				temp_value.SetString(tf.message.c_str(), tf.message.length(), allocator);
				f.AddMember("message", temp_value, allocator);
				files.PushBack(f, allocator);
			}
			t.AddMember("trackers", files, allocator);
			std::string temp_id = std::to_string(*it_ids);
			it_ids++;
			temp_value.SetString(temp_id.c_str(), temp_id.length(), allocator);
			torrents.AddMember(temp_value, t, allocator);	
		}
		document.AddMember("torrents", torrents, allocator);

		std::string json = stringfy_document(document);	

		if(accepts_gzip_encoding(request->header)) {
			ss_response << gzip_encode(json);
			http_header += "Content-Encoding: gzip\r\n";
		}
		else {
			ss_response << json;
		}
		http_header += "Content-Length: " + std::to_string(ss_response.str().length()) + "\r\n";
		http_header += "Content-Type: application/json\r\n";
		http_status = "200 OK";

		LOG_DEBUG << "HTTP " << request->method << " " << request->path << " "  << http_status
			<< " to " << request->remote_endpoint_address() << " Message: " << message;

		*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n" << ss_response.str();
	}
	else {
		rapidjson::Value errors(rapidjson::kArrayType);
		rapidjson::Value e(rapidjson::kObjectType);
		e.AddMember("code", 3250, allocator);
		char const *message = error_codes.find(3250)->second.c_str();
		e.AddMember("message", rapidjson::StringRef(message), allocator);
		e.AddMember("id", result, allocator);
		errors.PushBack(e, allocator);
		document.AddMember("errors", errors, allocator);

		std::string json = stringfy_document(document);

		if(accepts_gzip_encoding(request->header)) {
			ss_response << gzip_encode(json);
			http_header += "Content-Encoding: gzip\r\n";
		}
		else {
			ss_response << json;
		}

		http_header += "Content-Length: " + std::to_string(ss_response.str().length()) + "\r\n";
		http_header += "Content-Type: application/json\r\n";
		http_status = "404 Not Found";

		LOG_DEBUG << "HTTP " << request->method << " " << request->path << " "  << http_status
			<< " to " << request->remote_endpoint_address() << " Message: " << message;

		*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n" << ss_response.str();
	}
}

void RestAPI::program_status_get(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
	if(!validate_authorization(request)) {
		respond_invalid_authorization(response, request);
		return;
	}

	SessionStatus session_status = torrent_manager.get_session_status();

	std::string http_header;
	std::string origin_str;
	std::string credentials_str = "true";
	if(enable_CORS) {
		auto header = request->header;
		
		auto origin = header.find("Origin");
		if(origin != header.end()) {
			origin_str = origin->second;
		}

		http_header += "Access-Control-Allow-Origin: " + origin_str + "\r\n";
		http_header += "Access-Control-Allow-Credentials: " + credentials_str + "\r\n";
	}

	rapidjson::Document document;
	document.SetObject();
	rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
	std::string http_status;
	std::stringstream ss_response;
	// TODO - change the name of the variables here. I copied it from elsewhere
	char const *message = "Succesfuly retrieved program status";
	document.AddMember("message", rapidjson::StringRef(message), allocator);
	rapidjson::Value program(rapidjson::kObjectType);
	rapidjson::Value status(rapidjson::kObjectType);
	status.AddMember("has_incoming_connections", session_status.has_incoming_connections, allocator);
	status.AddMember("upload_rate", session_status.upload_rate, allocator);
	status.AddMember("download_rate", session_status.download_rate, allocator);
	status.AddMember("total_download", session_status.total_download, allocator);
	status.AddMember("total_upload", session_status.total_upload, allocator);
	status.AddMember("total_payload_download", session_status.total_payload_download, allocator);
	status.AddMember("total_payload_upload", session_status.total_payload_upload, allocator);
	status.AddMember("payload_download_rate", session_status.payload_download_rate, allocator);
	status.AddMember("payload_upload_rate", session_status.payload_upload_rate, allocator);
	status.AddMember("ip_overhead_upload_rate", session_status.ip_overhead_upload_rate, allocator);
	status.AddMember("ip_overhead_download_rate", session_status.ip_overhead_download_rate, allocator);
	status.AddMember("ip_overhead_upload", session_status.ip_overhead_upload, allocator);
	status.AddMember("ip_overhead_download", session_status.ip_overhead_download, allocator);
	status.AddMember("dht_upload_rate", session_status.dht_upload_rate, allocator);
	status.AddMember("dht_download_rate", session_status.dht_download_rate, allocator);
	status.AddMember("dht_nodes", session_status.dht_nodes, allocator);
	status.AddMember("dht_upload", session_status.dht_upload, allocator);
	status.AddMember("dht_download", session_status.dht_download, allocator);
	status.AddMember("tracker_upload_rate", session_status.tracker_upload_rate, allocator);
	status.AddMember("tracker_download_rate", session_status.tracker_download_rate, allocator);
	status.AddMember("tracker_upload", session_status.tracker_upload, allocator);
	status.AddMember("tracker_download", session_status.tracker_download, allocator);
	status.AddMember("num_peers_connected", session_status.num_peers_connected, allocator);
	status.AddMember("num_peers_half_open", session_status.num_peers_half_open, allocator);
	status.AddMember("total_peers_connections", session_status.total_peers_connections, allocator);
	program.AddMember("status", status, allocator);		
	document.AddMember("program", program, allocator);

	std::string json = stringfy_document(document);	

	if(accepts_gzip_encoding(request->header)) {
		ss_response << gzip_encode(json);
		http_header += "Content-Encoding: gzip\r\n";
	}
	else {
		ss_response << json;
	}
	http_header += "Content-Length: " + std::to_string(ss_response.str().length()) + "\r\n";
	http_header += "Content-Type: application/json\r\n";
	http_status = "200 OK";

	LOG_DEBUG << "HTTP " << request->method << " " << request->path << " "  << http_status
		<< " to " << request->remote_endpoint_address() << " Message: " << message;

	*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n" << ss_response.str();
}

void RestAPI::program_settings_get(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
	if(!validate_authorization(request)) {
		respond_invalid_authorization(response, request);
		return;
	}

	lt::settings_pack session_settings = torrent_manager.get_session_settings();

	std::string http_header;
	std::string origin_str;
	std::string credentials_str = "true";
	if(enable_CORS) {
		auto header = request->header;
		
		auto origin = header.find("Origin");
		if(origin != header.end()) {
			origin_str = origin->second;
		}

		http_header += "Access-Control-Allow-Origin: " + origin_str + "\r\n";
		http_header += "Access-Control-Allow-Credentials: " + credentials_str + "\r\n";
	}

	rapidjson::Document document;
	document.SetObject();
	rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
	std::string http_status;
	std::stringstream ss_response;
	// TODO - change the name of the variables here. I copied it from elsewhere
	char const *message = "Succesfuly retrieved program settings";
	document.AddMember("message", rapidjson::StringRef(message), allocator);
	rapidjson::Value program(rapidjson::kObjectType);
	rapidjson::Value settings(rapidjson::kObjectType);
	// TODO - there are A LOT of other fields that need to be here. Take a look at struct settings_pack
	// https://www.libtorrent.org/reference-Settings.html#settings_pack
	settings.AddMember("download_rate_limit", session_settings.get_int(lt::settings_pack::download_rate_limit), allocator);
	settings.AddMember("upload_rate_limit", session_settings.get_int(lt::settings_pack::upload_rate_limit), allocator);
	settings.AddMember("connections_limit", session_settings.get_int(lt::settings_pack::connections_limit), allocator);
	settings.AddMember("active_seeds", session_settings.get_int(lt::settings_pack::active_seeds), allocator);
	settings.AddMember("active_downloads", session_settings.get_int(lt::settings_pack::active_downloads), allocator);
	settings.AddMember("active_limit", session_settings.get_int(lt::settings_pack::active_limit), allocator);

	try 
		{
		rapidjson::Value temp_value;	
		std::string default_download_path = config.get_config<std::string>("directory.download_path"); 
		temp_value.SetString(default_download_path.c_str(), default_download_path.length(), allocator);
		settings.AddMember("default_download_path", temp_value, allocator);
	}
	catch(config_key_error const &e) {
		LOG_ERROR << "Problem initializing RestAPI. Could not get config: " << e.what();
		// TODO - respond error.
	}

	program.AddMember("settings", settings, allocator);		
	document.AddMember("program", program, allocator);

	std::string json = stringfy_document(document);	

	if(accepts_gzip_encoding(request->header)) {
		ss_response << gzip_encode(json);
		http_header += "Content-Encoding: gzip\r\n";
	}
	else {
		ss_response << json;
	}
	http_header += "Content-Length: " + std::to_string(ss_response.str().length()) + "\r\n";
	http_header += "Content-Type: application/json\r\n";
	http_status = "200 OK";

	LOG_DEBUG << "HTTP " << request->method << " " << request->path << " "  << http_status
		<< " to " << request->remote_endpoint_address() << " Message: " << message;

	*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n" << ss_response.str();
}


void RestAPI::torrents_info_get(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
	if(!validate_authorization(request)) {
		respond_invalid_authorization(response, request);
		return;
	}

	std::vector<unsigned long int> ids = split_string_to_ulong(request->path_match[1], ',');
	// TODO - Inefficient. When ids.size() = 0 should be treated inside the calls, not here.
	if(ids.size() == 0) // If no ids were specified, consider all ids
		ids = torrent_manager.get_all_ids(); 
	std::vector<boost::shared_ptr<const lt::torrent_info>> torrents_info;
	unsigned long int result = torrent_manager.get_torrents_info(torrents_info, ids);

	std::string http_header;
	std::string origin_str;
	std::string credentials_str = "true";
	if(enable_CORS) {
		auto header = request->header;
		
		auto origin = header.find("Origin");
		if(origin != header.end()) {
			origin_str = origin->second;
		}

		http_header += "Access-Control-Allow-Origin: " + origin_str + "\r\n";
		http_header += "Access-Control-Allow-Credentials: " + credentials_str + "\r\n";
	}

	rapidjson::Document document;
	document.SetObject();
	rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
	std::string http_status;
	std::stringstream ss_response;
	if(result == 0) {
		char const *message = "Succesfuly retrieved torrent info";
		document.AddMember("message", rapidjson::StringRef(message), allocator);
		rapidjson::Value torrents(rapidjson::kObjectType);
		std::vector<unsigned long int>::iterator it_ids= ids.begin();	
		for(boost::shared_ptr<const lt::torrent_info> ti : torrents_info) {
			rapidjson::Value t(rapidjson::kObjectType);
			rapidjson::Value s(rapidjson::kObjectType);
			rapidjson::Value temp_value;
			s.AddMember("total_size", ti->total_size(), allocator);
			s.AddMember("num_pieces", ti->num_pieces(), allocator);
			s.AddMember("piece_length", ti->piece_length(), allocator);
			/* info_hash: If this handle is to a torrent that hasn't loaded yet (for instance by being added) by a URL,
			   the returned value is undefined. */
			std::stringstream ss_info_hash;
			ss_info_hash << ti->info_hash();
			std::string info_hash = ss_info_hash.str();
			temp_value.SetString(info_hash.c_str(), info_hash.length(), allocator);
			s.AddMember("info_hash", temp_value, allocator);	
			s.AddMember("num_files", ti->num_files(), allocator);
			temp_value.SetString(ti->ssl_cert().c_str(), ti->ssl_cert().length(), allocator);
			s.AddMember("ssl_cert", temp_value, allocator);
			s.AddMember("priv", ti->priv(), allocator);
			s.AddMember("is_i2p", ti->is_i2p(), allocator);
			s.AddMember("is_loaded", ti->is_loaded(), allocator);
			if(ti->creation_date())
				s.AddMember("creation_date", ti->creation_date().get(), allocator);
			temp_value.SetString(ti->name().c_str(), ti->name().length(), allocator);
			s.AddMember("name", temp_value, allocator);
			temp_value.SetString(ti->comment().c_str(), ti->comment().length(), allocator);
			s.AddMember("comment", temp_value, allocator);
			temp_value.SetString(ti->creator().c_str(), ti->creator().length(), allocator);
			s.AddMember("creator", temp_value, allocator);
			t.AddMember("info", s, allocator);
			std::string temp_id = std::to_string(*it_ids);
			temp_value.SetString(temp_id.c_str(), temp_id.length(), allocator);
			it_ids++;
			torrents.AddMember(temp_value, t, allocator);
		}
		document.AddMember("torrents", torrents, allocator);

		std::string json = stringfy_document(document);	

		if(accepts_gzip_encoding(request->header)) {
			ss_response << gzip_encode(json);
			http_header += "Content-Encoding: gzip\r\n";
		}
		else {
			ss_response << json;
		}
		
		http_header += "Content-Length: " + std::to_string(ss_response.str().length()) + "\r\n";
		http_header += "Content-Type: application/json\r\n";
		http_status = "200 OK";

		LOG_DEBUG << "HTTP " << request->method << " " << request->path << " "  << http_status
			<< " to " << request->remote_endpoint_address() << " Message: " << message;

		*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n" << ss_response.str();
	}
	else {
		rapidjson::Value errors(rapidjson::kArrayType);
		rapidjson::Value e(rapidjson::kObjectType);
		e.AddMember("code", 3170, allocator);
		char const *message = error_codes.find(3170)->second.c_str();
		e.AddMember("message", rapidjson::StringRef(message), allocator);
		e.AddMember("id", result, allocator);
		errors.PushBack(e, allocator);
		document.AddMember("errors", errors, allocator);

		std::string json = stringfy_document(document);

		if(accepts_gzip_encoding(request->header)) {
			ss_response << gzip_encode(json);
			http_header += "Content-Encoding: gzip\r\n";
		}
		else {
			ss_response << json;
		}

		http_header += "Content-Length: " + std::to_string(ss_response.str().length()) + "\r\n";
		http_header += "Content-Type: application/json\r\n";
		http_status = "404 Not Found";

		LOG_DEBUG << "HTTP " << request->method << " " << request->path << " "  << http_status
			<< " to " << request->remote_endpoint_address() << " Message: " << message;

		*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n" << ss_response.str();
	}
}

void RestAPI::torrents_settings_get(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
	if(!validate_authorization(request)) {
		respond_invalid_authorization(response, request);
		return;
	}

	std::vector<unsigned long int> ids = split_string_to_ulong(request->path_match[1], ',');
	// TODO - Inefficient. When ids.size() = 0 should be treated inside the calls, not here.
	if(ids.size() == 0) // If no ids were specified, consider all ids
		ids = torrent_manager.get_all_ids(); 
	std::vector<Torrent::torrent_settings> torrents_settings;
	unsigned long int result = torrent_manager.get_settings_torrents(torrents_settings, ids);

	std::string http_header;
	std::string origin_str;
	std::string credentials_str = "true";
	if(enable_CORS) {
		auto header = request->header;
		
		auto origin = header.find("Origin");
		if(origin != header.end()) {
			origin_str = origin->second;
		}

		http_header += "Access-Control-Allow-Origin: " + origin_str + "\r\n";
		http_header += "Access-Control-Allow-Credentials: " + credentials_str + "\r\n";
	}

	rapidjson::Document document;
	document.SetObject();
	rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
	std::string http_status;
	std::stringstream ss_response;
	if(result == 0) {
		char const *message = "Succesfuly retrieved torrent settings";
		document.AddMember("message", rapidjson::StringRef(message), allocator);
		rapidjson::Value torrents(rapidjson::kObjectType);
		std::vector<unsigned long int>::iterator it_ids= ids.begin();	
		for(Torrent::torrent_settings ts : torrents_settings) {
			rapidjson::Value t(rapidjson::kObjectType);
			rapidjson::Value s(rapidjson::kObjectType);
			rapidjson::Value temp_value;
			if(ts.upload_limit)
				s.AddMember("upload_limit", ts.upload_limit.get(), allocator);
			if(ts.download_limit)
				s.AddMember("download_limit", ts.download_limit.get(), allocator);
			if(ts.sequential_download)
				s.AddMember("sequential_download", ts.sequential_download.get(), allocator);
			t.AddMember("settings", s, allocator);
			std::string temp_id = std::to_string(*it_ids);
			temp_value.SetString(temp_id.c_str(), temp_id.length(), allocator);
			it_ids++;
			torrents.AddMember(temp_value, t, allocator);
		}
		document.AddMember("torrents", torrents, allocator);

		std::string json = stringfy_document(document);	

		if(accepts_gzip_encoding(request->header)) {
			ss_response << gzip_encode(json);
			http_header += "Content-Encoding: gzip\r\n";
		}
		else {
			ss_response << json;
		}
		
		http_header += "Content-Length: " + std::to_string(ss_response.str().length()) + "\r\n";
		http_header += "Content-Type: application/json\r\n";
		http_status = "200 OK";

		LOG_DEBUG << "HTTP " << request->method << " " << request->path << " "  << http_status
			<< " to " << request->remote_endpoint_address() << " Message: " << message;

		*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n" << ss_response.str();
	}
	else {
		rapidjson::Value errors(rapidjson::kArrayType);
		rapidjson::Value e(rapidjson::kObjectType);
		e.AddMember("code", 3260, allocator);
		char const *message = error_codes.find(3260)->second.c_str();
		e.AddMember("message", rapidjson::StringRef(message), allocator);
		e.AddMember("id", result, allocator);
		errors.PushBack(e, allocator);
		document.AddMember("errors", errors, allocator);

		std::string json = stringfy_document(document);

		if(accepts_gzip_encoding(request->header)) {
			ss_response << gzip_encode(json);
			http_header += "Content-Encoding: gzip\r\n";
		}
		else {
			ss_response << json;
		}

		http_header += "Content-Length: " + std::to_string(ss_response.str().length()) + "\r\n";
		http_header += "Content-Type: application/json\r\n";
		http_status = "404 Not Found";

		LOG_DEBUG << "HTTP " << request->method << " " << request->path << " "  << http_status
			<< " to " << request->remote_endpoint_address() << " Message: " << message;

		*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n" << ss_response.str();
	}
}

void RestAPI::torrents_settings_set(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
	if(!validate_authorization(request)) {
		respond_invalid_authorization(response, request);
		return;
	}

	rapidjson::Document document;
	rapidjson::ParseResult parse_ok = document.Parse(request->content.string().c_str());

	if(!parse_ok) {
		LOG_ERROR <<  "JSON parse error: " <<  rapidjson::GetParseError_En(parse_ok.Code()) << "(" << parse_ok.Offset() << ")";
		// TODO - respond invalid json parse	
	}
	
	// TODO - wrap this in a function	
	std::vector<unsigned long int> ids;
	std::vector<Torrent::torrent_settings> torrents_settings;
	for(auto &torrent : document["torrents"].GetObject()) { // TODO - What if we cant find it in document? LOG and respond error
		Torrent::torrent_settings ts;
		for(auto &setting : document["torrents"][torrent.name]["settings"].GetObject()) { // TODO - What if we cant find it in document? LOG and respond error
			std::string setting_name = setting.name.GetString();
			rapidjson::Value &setting_value = setting.value;
			if(setting_value.IsInt()) {
				int value = setting_value.GetInt();
				if(setting_name == "upload_limit")
					ts.upload_limit = value;
				else if(setting_name == "download_limit")
					ts.download_limit = value;
				else {
					// TODO - Unknown setting name. Do something about that. Log and send response.
				}
			}
			if(setting_value.IsBool()) {
				bool value = setting_value.GetBool();
				if(setting_name == "sequential_download")
					ts.sequential_download = value;
				else {
					// TODO - Unknown setting name. Do something about that. Log and send response.
				}
			}

			// TODO - stoul could go wrong. Treat this.
			ids.push_back(std::stoul(torrent.name.GetString()));
		       	torrents_settings.push_back(ts);	
		}
	}

	unsigned long int result = torrent_manager.set_settings_torrents(torrents_settings, ids);

	std::string http_header;
	std::string origin_str;
	std::string credentials_str = "true";
	if(enable_CORS) {
		auto header = request->header;
		
		auto origin = header.find("Origin");
		if(origin != header.end()) {
			origin_str = origin->second;
		}

		http_header += "Access-Control-Allow-Origin: " + origin_str + "\r\n";
		http_header += "Access-Control-Allow-Credentials: " + credentials_str + "\r\n";
	}

	document = rapidjson::Document();
	document.SetObject();
	rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
	std::string http_status;
	std::stringstream ss_response;
	if(result == 0) {
		char const *message = "Succesfuly changed torrent settings";
		document.AddMember("message", rapidjson::StringRef(message), allocator);
		std::string json = stringfy_document(document);	
		if(accepts_gzip_encoding(request->header)) {
			ss_response << gzip_encode(json);
			http_header += "Content-Encoding: gzip\r\n";
		}
		else {
			ss_response << json;
		}
		
		http_header += "Content-Length: " + std::to_string(ss_response.str().length()) + "\r\n";
		http_header += "Content-Type: application/json\r\n";
		http_status = "200 OK";

		LOG_DEBUG << "HTTP " << request->method << " " << request->path << " "  << http_status
			<< " to " << request->remote_endpoint_address() << " Message: " << message;

		*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n" << ss_response.str();
	}
	else {
		rapidjson::Value errors(rapidjson::kArrayType);
		rapidjson::Value e(rapidjson::kObjectType);
		e.AddMember("code", 3270, allocator);
		char const *message = error_codes.find(3270)->second.c_str();
		e.AddMember("message", rapidjson::StringRef(message), allocator);
		e.AddMember("id", result, allocator);
		errors.PushBack(e, allocator);
		document.AddMember("errors", errors, allocator);

		std::string json = stringfy_document(document);

		if(accepts_gzip_encoding(request->header)) {
			ss_response << gzip_encode(json);
			http_header += "Content-Encoding: gzip\r\n";
		}
		else {
			ss_response << json;
		}

		http_header += "Content-Length: " + std::to_string(ss_response.str().length()) + "\r\n";
		http_header += "Content-Type: application/json\r\n";
		http_status = "404 Not Found";

		LOG_DEBUG << "HTTP " << request->method << " " << request->path << " "  << http_status
			<< " to " << request->remote_endpoint_address() << " Message: " << message;

		*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n" << ss_response.str();
	}
}

void RestAPI::program_settings_set(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
	if(!validate_authorization(request)) {
		respond_invalid_authorization(response, request);
		return;
	}

	rapidjson::Document document;
	rapidjson::ParseResult parse_ok = document.Parse(request->content.string().c_str());

	if(!parse_ok) {
		LOG_ERROR <<  "JSON parse error: " <<  rapidjson::GetParseError_En(parse_ok.Code()) << "(" << parse_ok.Offset() << ")";
		// TODO - respond invalid json parse	
	}
	
	// TODO - wrap this in a function
	lt::settings_pack pack;
	for(auto &setting : document["program"]["settings"].GetObject()) { // TODO - What if we cant find it in document? LOG and respond error
		std::string setting_name = setting.name.GetString();
		rapidjson::Value &setting_value = setting.value;
		if(setting_value.IsInt()) {
			int value = setting_value.GetInt();
			if(setting_name == "download_rate_limit")
				pack.set_int(lt::settings_pack::download_rate_limit, value);
			else if(setting_name == "upload_rate_limit")
				pack.set_int(lt::settings_pack::upload_rate_limit, value);
			else if(setting_name == "connections_limit")
				pack.set_int(lt::settings_pack::connections_limit, value);
			else if(setting_name == "active_seeds")
				pack.set_int(lt::settings_pack::active_seeds, value);
			else if(setting_name == "active_downloads")
				pack.set_int(lt::settings_pack::active_downloads, value);
			else if(setting_name == "active_limit")
				pack.set_int(lt::settings_pack::active_limit, value);
			else {
				// TODO - Unknown setting name. Do something about that. Log and send response.
			}
		}
		else if(setting_value.IsString()) {
			std::string value = setting_value.GetString();
			// TODO - find a solution to the following problem:
			// settings configs may need the restart of the program. Currently we only set the default_download_path, so 
			// no restart is needed, but in the future we will need to notify the user that the program will need a restart to apply
			// changes.
			if(setting_name == "default_download_path") {
				config.set_config<std::string>("directory", "download_path", value);	
			}
			else {
				// TODO - Unknown setting name. Do something about that. Log and send response.
			}
		}
	}
	
	unsigned long int result = torrent_manager.set_session_settings(pack);

	std::string http_header;
	std::string origin_str;
	std::string credentials_str = "true";
	if(enable_CORS) {
		auto header = request->header;
		
		auto origin = header.find("Origin");
		if(origin != header.end()) {
			origin_str = origin->second;
		}

		http_header += "Access-Control-Allow-Origin: " + origin_str + "\r\n";
		http_header += "Access-Control-Allow-Credentials: " + credentials_str + "\r\n";
	}

	document = rapidjson::Document();
	document.SetObject();
	rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
	std::string http_status;
	std::stringstream ss_response;
	if(result == 0) {
		char const *message = "Succesfuly changed program settings";
		document.AddMember("message", rapidjson::StringRef(message), allocator);
		std::string json = stringfy_document(document);	
		if(accepts_gzip_encoding(request->header)) {
			ss_response << gzip_encode(json);
			http_header += "Content-Encoding: gzip\r\n";
		}
		else {
			ss_response << json;
		}
		
		http_header += "Content-Length: " + std::to_string(ss_response.str().length()) + "\r\n";
		http_header += "Content-Type: application/json\r\n";
		http_status = "200 OK";

		LOG_DEBUG << "HTTP " << request->method << " " << request->path << " "  << http_status
			<< " to " << request->remote_endpoint_address() << " Message: " << message;

		*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n" << ss_response.str();
	}
	else {
		rapidjson::Value errors(rapidjson::kArrayType);
		rapidjson::Value e(rapidjson::kObjectType);
		e.AddMember("code", 3280, allocator);
		char const *message = error_codes.find(3280)->second.c_str();
		e.AddMember("message", rapidjson::StringRef(message), allocator);
		e.AddMember("id", result, allocator);
		errors.PushBack(e, allocator);
		document.AddMember("errors", errors, allocator);

		std::string json = stringfy_document(document);

		if(accepts_gzip_encoding(request->header)) {
			ss_response << gzip_encode(json);
			http_header += "Content-Encoding: gzip\r\n";
		}
		else {
			ss_response << json;
		}

		http_header += "Content-Length: " + std::to_string(ss_response.str().length()) + "\r\n";
		http_header += "Content-Type: application/json\r\n";
		http_status = "404 Not Found";

		LOG_DEBUG << "HTTP " << request->method << " " << request->path << " "  << http_status
			<< " to " << request->remote_endpoint_address() << " Message: " << message;

		*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n" << ss_response.str();
	}
}

void RestAPI::queue_torrents_set(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
	if(!validate_authorization(request)) {
		respond_invalid_authorization(response, request);
		return;
	}

	std::vector<unsigned long int> ids = split_string_to_ulong(request->path_match[1], ',');
	if(ids.size() != 1) {
		// TODO - error. Log and respond. Only 1 id allowed. 	
	}

	rapidjson::Document document;
	rapidjson::ParseResult parse_ok = document.Parse(request->content.string().c_str());

	if(!parse_ok) {
		LOG_ERROR <<  "JSON parse error: " <<  rapidjson::GetParseError_En(parse_ok.Code()) << "(" << parse_ok.Offset() << ")";
		// TODO - respond invalid json parse	
	}
	
	// TODO - We need to MAKE SURE queue_position is in the json object. If its not we must return error, but the way this piece of code is structured makes it difficult to do that. Probably I need to refactor this to a more general solution like the one with using with the api parameter. There are many other times this piece of code is used and with need a general solution for that and for required and optional fields.
	// TODO - wrap this in a function.
	std::string queue_position = "-1";
	for(auto &setting : document.GetObject()) { // TODO - What if we cant find it in document? LOG and respond error
		std::string setting_name = setting.name.GetString();
		rapidjson::Value &setting_value = setting.value;
		if(setting_value.IsInt()) {
			int value = setting_value.GetInt();
			if(setting_name == "queue_position")
				queue_position = std::to_string(value);
			else {
				// TODO - Unknown setting name. Do something about that. Log and send response.
			}
		}
		if(setting_value.IsString()) {
			std::string value = setting_value.GetString();
			if(setting_name == "queue_position")
				queue_position = value;
			else {
				// TODO - Unknown setting name. Do something about that. Log and send response.
			}
		}
	}
	
	unsigned long int result = torrent_manager.set_session_queue(queue_position, ids);

	std::string http_header;
	std::string origin_str;
	std::string credentials_str = "true";
	if(enable_CORS) {
		auto header = request->header;
		
		auto origin = header.find("Origin");
		if(origin != header.end()) {
			origin_str = origin->second;
		}

		http_header += "Access-Control-Allow-Origin: " + origin_str + "\r\n";
		http_header += "Access-Control-Allow-Credentials: " + credentials_str + "\r\n";
	}

	document = rapidjson::Document();
	document.SetObject();
	rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
	std::string http_status;
	std::stringstream ss_response;
	if(result == 0) {
		char const *message = "Succesfuly changed queue position";
		document.AddMember("message", rapidjson::StringRef(message), allocator);
		std::string json = stringfy_document(document);	
		if(accepts_gzip_encoding(request->header)) {
			ss_response << gzip_encode(json);
			http_header += "Content-Encoding: gzip\r\n";
		}
		else {
			ss_response << json;
		}
		
		http_header += "Content-Length: " + std::to_string(ss_response.str().length()) + "\r\n";
		http_header += "Content-Type: application/json\r\n";
		http_status = "200 OK";

		LOG_DEBUG << "HTTP " << request->method << " " << request->path << " "  << http_status
			<< " to " << request->remote_endpoint_address() << " Message: " << message;

		*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n" << ss_response.str();
	}
	else {
		rapidjson::Value errors(rapidjson::kArrayType);
		rapidjson::Value e(rapidjson::kObjectType);
		e.AddMember("code", 3290, allocator);
		char const *message = error_codes.find(3290)->second.c_str();
		e.AddMember("message", rapidjson::StringRef(message), allocator);
		e.AddMember("id", result, allocator);
		errors.PushBack(e, allocator);
		document.AddMember("errors", errors, allocator);

		std::string json = stringfy_document(document);

		if(accepts_gzip_encoding(request->header)) {
			ss_response << gzip_encode(json);
			http_header += "Content-Encoding: gzip\r\n";
		}
		else {
			ss_response << json;
		}

		http_header += "Content-Length: " + std::to_string(ss_response.str().length()) + "\r\n";
		http_header += "Content-Type: application/json\r\n";
		http_status = "404 Not Found";

		LOG_DEBUG << "HTTP " << request->method << " " << request->path << " "  << http_status
			<< " to " << request->remote_endpoint_address() << " Message: " << message;

		*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n" << ss_response.str();
	}
}

// TODO - this is TEMPORARY. a GET /authorization method to allow the front-end to verify the login. In the future we need to create a GET /token
// similar to the PayPal API and work with access tokens. https://developer.paypal.com/docs/api/overview/#make-your-first-call
// I dont like the way the responses to this request work. Improve. 
void RestAPI::get_authorization(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
	if(!validate_authorization(request)) {
		respond_invalid_authorization(response, request);
		return;
	}

	std::string http_header;
	std::string origin_str;
	std::string credentials_str = "true";
	if(enable_CORS) {
		auto header = request->header;
		
		auto origin = header.find("Origin");
		if(origin != header.end()) {
			origin_str = origin->second;
		}

		http_header += "Access-Control-Allow-Origin: " + origin_str + "\r\n";
		http_header += "Access-Control-Allow-Credentials: " + credentials_str + "\r\n";
	}

	rapidjson::Document document;
	document.SetObject();
	rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
	std::string http_status;
	std::stringstream ss_response;
	char const *message = "valid Authorization. Access allowed";
	document.AddMember("message", rapidjson::StringRef(message), allocator);

	std::string json = stringfy_document(document);	

	if(accepts_gzip_encoding(request->header)) {
		ss_response << gzip_encode(json);
		http_header += "Content-Encoding: gzip\r\n";
	}
	else {
		ss_response << json;
	}
	http_header += "Content-Length: " + std::to_string(ss_response.str().length()) + "\r\n";
	http_header += "Content-Type: application/json\r\n";
	http_status = "200 OK";

	LOG_DEBUG << "HTTP " << request->method << " " << request->path << " "  << http_status
		<< " to " << request->remote_endpoint_address() << " Message: " << message;

	*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n" << ss_response.str();
}


// TODO - in the future we should receive a parameter to describe if we want to send the directory files also. Currently it only
// shows the directories inside the directory. This is sufficient for now, but in the future we will need this parameter to show the files also
// I think.
void RestAPI::server_directory_get(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
	if(!validate_authorization(request)) {
		respond_invalid_authorization(response, request);
		return;
	}

	std::map<std::string, api_parameter> required_parameters = {
		{"path",{"path",".",api_parameter_format::text,{}}}
	};
	std::map<std::string, api_parameter> optional_parameters = { };
	SimpleWeb::CaseInsensitiveMultimap query = request->parse_query_string();
	std::string invalid_parameter = validate_all_parameters(query, required_parameters, optional_parameters);
	if(invalid_parameter.length() > 0) { 
		respond_invalid_parameter(response, request, invalid_parameter);
		return;
	}

	std::string http_header;
	std::string origin_str;
	std::string credentials_str = "true";
	if(enable_CORS) {
		auto header = request->header;
		
		auto origin = header.find("Origin");
		if(origin != header.end()) {
			origin_str = origin->second;
		}

		http_header += "Access-Control-Allow-Origin: " + origin_str + "\r\n";
		http_header += "Access-Control-Allow-Credentials: " + credentials_str + "\r\n";
	}

	rapidjson::Document document;
	document.SetObject();
	rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
	std::string http_status;
	std::stringstream ss_response;
	fs::path p = required_parameters.find("path")->second.value;
	// TODO - What if user has no permission to read directory? Error? Test this.
	if(fs::is_directory(p)) {
		rapidjson::Value temp_value;

		std::string parent_type = "directory";
		temp_value.SetString(parent_type.c_str(), parent_type.length(), allocator);
		document.AddMember("type", temp_value, allocator);

		std::string parent_name = p.filename().string();
		temp_value.SetString(parent_name.c_str(), parent_name.length(), allocator);
		document.AddMember("name", temp_value, allocator);

		std::string parent_path = (fs::canonical(p)).string();
		temp_value.SetString(parent_path.c_str(), parent_path.length(), allocator);
		document.AddMember("path", temp_value, allocator);

		std::string parent_parent_path = (fs::canonical(p)).parent_path().string();
		temp_value.SetString(parent_parent_path.c_str(), parent_parent_path.length(), allocator);
		document.AddMember("parent_path", temp_value, allocator);

		rapidjson::Value dir_content(rapidjson::kArrayType);
		for(auto& entry : boost::make_iterator_range(fs::directory_iterator(p), {})) {
			rapidjson::Value dir(rapidjson::kObjectType);
			if(fs::is_directory(entry)) {
				std::string parent_type = "directory";
				temp_value.SetString(parent_type.c_str(), parent_type.length(), allocator);
				dir.AddMember("type", temp_value, allocator);

				std::string name = (fs::path(entry)).filename().string();
				temp_value.SetString(name.c_str(), name.length(), allocator);
				dir.AddMember("name", temp_value, allocator);

				std::string path = (fs::canonical(fs::path(entry))).string();
				temp_value.SetString(path.c_str(), path.length(), allocator);
				dir.AddMember("path", temp_value, allocator);
				
				dir_content.PushBack(dir, allocator);
			}
		}
		document.AddMember("content", dir_content, allocator);
	}
	else {
		// TODO - log/respond. path is not directory
	}

	std::string json = stringfy_document(document);	

	if(accepts_gzip_encoding(request->header)) {
		ss_response << gzip_encode(json);
		http_header += "Content-Encoding: gzip\r\n";
	}
	else {
		ss_response << json;
	}
	http_header += "Content-Length: " + std::to_string(ss_response.str().length()) + "\r\n";
	http_header += "Content-Type: application/json\r\n";
	http_status = "200 OK";

	LOG_DEBUG << "HTTP " << request->method << " " << request->path << " "  << http_status
		<< " to " << request->remote_endpoint_address();

	*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n" << ss_response.str();
}

// TODO - This is INSECURE. The Client has access to the entire filesystem. Fix this allowing only access to the torrents folders and files.
// TODO - As far as I know the video file here is being sent sequentially, therefore the client will have problems jumping forward in the torrent video stream (more noticeably in slower and remote connections). This is ok for now, another approach should be implemented soon. Search for HTTP 206 Partial Content and streaming!! ( I am not sure about that. Test before in a slow, non localhost connection)
void RestAPI::stream_get(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {

	// TODO - VERY IMPORTANT. ADD THE AUTHORIZATION CHECK HERE. 

	try {
		std::map<std::string, api_parameter> required_parameters = {
			{"path",{"path",".",api_parameter_format::text,{}}}
		};
		std::map<std::string, api_parameter> optional_parameters = { };
		SimpleWeb::CaseInsensitiveMultimap query = request->parse_query_string();
		std::string invalid_parameter = validate_all_parameters(query, required_parameters, optional_parameters);
		if(invalid_parameter.length() > 0) { 
			respond_invalid_parameter(response, request, invalid_parameter);
			return;
		}

		fs::path path = required_parameters.find("path")->second.value;
		if(!boost::filesystem::is_regular_file(path)) { // TODO - there is also a funtion called is_symlink(). Maybe we should permit symlinks and not only regular files? Test this. 
			// TODO - error. log. respond. Is not file. Needs to be a file.
		}

		SimpleWeb::CaseInsensitiveMultimap header;

		//    Uncomment the following line to enable Cache-Control
		//    header.emplace("Cache-Control", "max-age=86400");

#ifdef HAVE_OPENSSL
		//    Uncomment the following lines to enable ETag
		//    {
		//      ifstream ifs(path.string(), ifstream::in | ios::binary);
		//      if(ifs) {
		//        auto hash = SimpleWeb::Crypto::to_hex_string(SimpleWeb::Crypto::md5(ifs));
		//        header.emplace("ETag", "\"" + hash + "\"");
		//        auto it = request->header.find("If-None-Match");
		//        if(it != request->header.end()) {
		//          if(!it->second.empty() && it->second.compare(1, hash.size(), hash) == 0) {
		//            response->write(SimpleWeb::StatusCode::redirection_not_modified, header);
		//            return;
		//          }
		//        }
		//      }
		//      else
		//        throw invalid_argument("could not read file");
		//    }
#endif

		auto ifs = std::make_shared<std::ifstream>();
		ifs->open(path.string(), std::ifstream::in | std::ios::binary | std::ios::ate);

		if(*ifs) {
			auto length = ifs->tellg();
			ifs->seekg(0, std::ios::beg);

			header.emplace("Content-Length", std::to_string(length)); // TODO - There are more headers that SHOULD be here so clients will know the filename, file extension, video format etc. Like Content-Disposition. Google about this. What headers are needed to stream? 
			response->write(header);

			// Trick to define a recursive function within this scope (for your convenience)
			class FileServer {
				public:
					static void read_and_send(const std::shared_ptr<HttpServer::Response> &response, const std::shared_ptr<std::ifstream> &ifs) {
						// Read and send 128 KB at a time
						static std::vector<char> buffer(131072); // Safe when server is running on one thread
						std::streamsize read_length;
						if((read_length = ifs->read(&buffer[0], buffer.size()).gcount()) > 0) {
							response->write(&buffer[0], read_length);
							if(read_length == static_cast<std::streamsize>(buffer.size())) {
								response->send([response, ifs](const SimpleWeb::error_code &ec) {
										if(!ec)
										read_and_send(response, ifs);
										else
										std::cerr << "Connection interrupted" << std::endl; // TODO - log
										});
							}
						}
					}
			};
			FileServer::read_and_send(response, ifs);
		}
		else
			throw std::invalid_argument("could not read file");
	}
	catch(const std::exception &e) {
		response->write(SimpleWeb::StatusCode::client_error_bad_request, "Could not open path " + request->path + ": " + e.what());
		LOG_ERROR << "Could not open path " + request->path + ": " + e.what();
	}
}
