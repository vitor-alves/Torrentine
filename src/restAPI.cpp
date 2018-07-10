#include <boost/filesystem.hpp>
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
			
			/* TODO - This is currently not used (its enabled but does not work as expected cuz header is empty) because I think there is a bug here. 
			 * request->header in always empty! I opened and issue.
			 * https://gitlab.com/eidheim/Simple-Web-Server/issues/233
			 * After this is fixed the variables here should be used to write the
			 * http header in the response. Allow-origin should use origin_str,
			 * Allow-methods should use request_headers_str,
			 * allow-headers should use request_methods_str.
			 * Currently this is hard coded... */
			std::string origin_str = origin_str_default ;
			std::string request_headers_str = request_headers_str_default;
			std::string request_methods_str = request_methods_str_default;
			std::string credentials_str = request_credentials_str_default;
			if(enable_CORS) {
				resp = "CORS is enabled";

				auto header = SimpleWeb::HttpHeader::parse(request->content);
				
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

	/* /torrents/<id>/stop - PATCH */
	server.resource["^/v1.0/session/torrents/(?:([0-9,]*)/|)stop$"]["PATCH"] =
		[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) 
		{ this->torrents_stop(response, request); };

	/* /torrents/<id>/files - GET */
	server.resource["^/v1.0/session/torrents/(?:([0-9,]*)/|)files$"]["GET"] =
		[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) 
		{ this->torrents_files_get(response, request); };

	/* /torrents/<id>/peers - GET */
	server.resource["^/v1.0/session/torrents/(?:([0-9,]*)/|)peers$"]["GET"] =
		[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) 
		{ this->torrents_peers_get(response, request); };

	/* /torrents/<id>/recheck - POST */
	server.resource["^/v1.0/session/torrents/(?:([0-9,]*)/|)recheck$"]["POST"] =
		[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) 
		{ this->torrents_recheck(response, request); };

	/* /torrents/<id>/start - PATCH */
	server.resource["^/v1.0/session/torrents/(?:([0-9,]*)/|)start$"]["PATCH"] =
		[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) 
		{ this->torrents_start(response, request); };

	/* /session/torrents/<id>/status - GET */
	server.resource["^/v1.0/session/torrents/(?:([0-9,]*)/|)status$"]["GET"] =
		[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) 
		{ this->torrents_status_get(response, request); };

	/* /session/torrents/<id> - DELETE */
	server.resource["^/v1.0/session/torrents(?:/([0-9,]+)|)$"]["DELETE"] =
		[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request)
		{ this->torrents_delete(response, request); };

	/* /logs - GET */
	server.resource["^/v1.0/logs$"]["GET"] =
		[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request)
		{ this->get_logs(response, request); };

	/* /session/torrents - POST */
	server.resource["^/v1.0/session/torrents$"]["POST"] =
		[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) 
		{ this->torrents_add(response, request); };

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
	std::string origin_str = origin_str_default ;
	std::string request_headers_str = request_headers_str_default;
	std::string request_methods_str = request_methods_str_default;
	std::string credentials_str = request_credentials_str_default;
	if(enable_CORS) {
		auto header = SimpleWeb::HttpHeader::parse(request->content);
		
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
	std::string origin_str = origin_str_default ;
	std::string request_headers_str = request_headers_str_default;
	std::string request_methods_str = request_methods_str_default;
	std::string credentials_str = request_credentials_str_default;
	if(enable_CORS) {
		auto header = SimpleWeb::HttpHeader::parse(request->content);
		
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
	std::string origin_str = origin_str_default ;
	std::string request_headers_str = request_headers_str_default;
	std::string request_methods_str = request_methods_str_default;
	std::string credentials_str = request_credentials_str_default;
	if(enable_CORS) {
		auto header = SimpleWeb::HttpHeader::parse(request->content);
		
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
		http_header += "Content-Disposition: inline; filename=bitsleek-log.txt\r\n";
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
	std::string origin_str = origin_str_default ;
	std::string request_headers_str = request_headers_str_default;
	std::string request_methods_str = request_methods_str_default;
	std::string credentials_str = request_credentials_str_default;
	if(enable_CORS) {
		auto header = SimpleWeb::HttpHeader::parse(request->content);
		
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

	rapidjson::Document document;
	document.SetObject();
	rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
	std::string http_status;
	std::stringstream ss_response;
	if(result == 0) {
		char const *message = "Succesfuly retrieved torrent peers";
		document.AddMember("message", rapidjson::StringRef(message), allocator);
		rapidjson::Value torrents(rapidjson::kArrayType);
		std::vector<unsigned long int>::iterator it_ids= ids.begin();	
		for(std::vector<Torrent::torrent_peer> torrent_peers : requested_torrent_peers) {
			rapidjson::Value t(rapidjson::kObjectType);
			t.AddMember("id", *it_ids, allocator);
			it_ids++;
			rapidjson::Value peers(rapidjson::kArrayType);
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
			torrents.PushBack(t, allocator);
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
	std::string origin_str = origin_str_default ;
	std::string request_headers_str = request_headers_str_default;
	std::string request_methods_str = request_methods_str_default;
	std::string credentials_str = request_credentials_str_default;
	if(enable_CORS) {
		auto header = SimpleWeb::HttpHeader::parse(request->content);
		
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

	rapidjson::Document document;
	document.SetObject();
	rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
	std::string http_status;
	std::stringstream ss_response;
	if(result == 0) {
		char const *message = "Succesfuly retrieved torrent files";
		document.AddMember("message", rapidjson::StringRef(message), allocator);
		rapidjson::Value torrents(rapidjson::kArrayType);
		std::vector<unsigned long int>::iterator it_ids= ids.begin();	
		for(std::vector<Torrent::torrent_file> torrent_files : requested_torrent_files) {
			rapidjson::Value t(rapidjson::kObjectType);
			t.AddMember("id", *it_ids, allocator);
			it_ids++;
			rapidjson::Value files(rapidjson::kArrayType);
			for(Torrent::torrent_file tf : torrent_files) {
				rapidjson::Value f(rapidjson::kObjectType);
				rapidjson::Value name;
				name.SetString(tf.name.c_str(), tf.name.size(), allocator);
				// TODO - this works, but I do not know if this is sufficient to implement the UI file tree view in JS easily.
				// Maybe I will need to tweak this later. Deluge also sends a field "type" that specifies if its a dir or a file.
				// Not sure if I need that, but keep that in mind.	
				f.AddMember("index", tf.index, allocator);
				f.AddMember("name", name, allocator);
				f.AddMember("progress", double(tf.progress)/double(tf.size), allocator);
				f.AddMember("downloaded_total", tf.progress, allocator);
				f.AddMember("size", tf.size, allocator);
				f.AddMember("priority", tf.priority, allocator);
				rapidjson::Value path;
				path.SetString(tf.path.c_str(), tf.path.size(), allocator);
				f.AddMember("path", path, allocator);
				files.PushBack(f, allocator);
			}
			t.AddMember("files", files, allocator);
			torrents.PushBack(t, allocator);
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
	std::string origin_str = origin_str_default ;
	std::string request_headers_str = request_headers_str_default;
	std::string request_methods_str = request_methods_str_default;
	std::string credentials_str = request_credentials_str_default;
	if(enable_CORS) {
		auto header = SimpleWeb::HttpHeader::parse(request->content);
		
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

// TODO - add a "ratio" field also in the json
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
	std::string origin_str = origin_str_default ;
	std::string request_headers_str = request_headers_str_default;
	std::string request_methods_str = request_methods_str_default;
	std::string credentials_str = request_credentials_str_default;
	if(enable_CORS) {
		auto header = SimpleWeb::HttpHeader::parse(request->content);
		
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

	rapidjson::Document document;
	document.SetObject();
	rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
	std::string http_status;
	std::stringstream ss_response;
	if(result == 0) {
		char const *message = "Succesfuly retrieved torrent status";
		document.AddMember("message", rapidjson::StringRef(message), allocator);
		rapidjson::Value torrents(rapidjson::kArrayType);
		std::vector<unsigned long int>::iterator it_ids= ids.begin();	
		for(lt::torrent_status status : torrents_status) {
			lt::torrent_handle handle = status.handle;
			rapidjson::Value t(rapidjson::kObjectType);
			t.AddMember("id", *it_ids, allocator);
			it_ids++;
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
			s.AddMember("next_announce", lt::duration_cast<lt::milliseconds>(status.next_announce).count(), allocator);
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
			torrents.PushBack(t, allocator);
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
	std::string origin_str = origin_str_default ;
	std::string request_headers_str = request_headers_str_default;
	std::string request_methods_str = request_methods_str_default;
	std::string credentials_str = request_credentials_str_default;
	if(enable_CORS) {
		auto header = SimpleWeb::HttpHeader::parse(request->content);
		
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
	}

	std::string http_header;
	std::string origin_str = origin_str_default ;
	std::string request_headers_str = request_headers_str_default;
	std::string request_methods_str = request_methods_str_default;
	std::string credentials_str = request_credentials_str_default;
	if(enable_CORS) {
		auto header = SimpleWeb::HttpHeader::parse(request->content);
		
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
										std::cerr << "Connection interrupted" << std::endl;
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
	std::string origin_str = origin_str_default ;
	std::string request_headers_str = request_headers_str_default;
	std::string request_methods_str = request_methods_str_default;
	std::string credentials_str = request_credentials_str_default;
	if(enable_CORS) {
		auto header = SimpleWeb::HttpHeader::parse(request->content);
		
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
	std::string origin_str = origin_str_default ;
	std::string request_headers_str = request_headers_str_default;
	std::string request_methods_str = request_methods_str_default;
	std::string credentials_str = request_credentials_str_default;
	if(enable_CORS) {
		auto header = SimpleWeb::HttpHeader::parse(request->content);
		
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
