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
	/* /torrents/<id>/stop - PATCH */
	server.resource["^/v1.0/session/torrents/(?:([0-9,]*)/|)stop$"]["PATCH"] =
	      	[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) 
		{ this->torrents_stop(response, request); };

	/* /torrents/<id>/files - GET */
	server.resource["^/v1.0/session/torrents/(?:([0-9,]*)/|)files$"]["GET"] =
	      	[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) 
		{ this->torrents_files_get(response, request); };

	/* /torrents/<id>/recheck - POST */
	server.resource["^/v1.0/session/torrents/(?:([0-9,]*)/|)recheck$"]["POST"] =
	      	[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) 
		{ this->torrents_recheck(response, request); };

	/* /torrents/<id>/start - PATCH */
	server.resource["^/v1.0/session/torrents/(?:([0-9,]*)/|)start$"]["PATCH"] =
	      	[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) 
		{ this->torrents_start(response, request); };

	/* /session/torrents/<id> - GET */
	server.resource["^/v1.0/session/torrents(?:/([0-9,]+)|)$"]["GET"] =
		[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) 
		{ this->torrents_get(response, request); };

	/* /session/torrents/<id> - DELETE */
	server.resource["^/v1.0/session/torrents(?:/([0-9,]+)|)$"]["DELETE"] =
		[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request)
		{ this->torrents_delete(response, request); };

	/* /logs - GET */
	server.resource["^/v1.0/logs$"]["GET"] =
		[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request)
		{ this->get_logs(response, request); };

	server.resource["^/v1.0/session/torrents$"]["POST"] =
	       	[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) 
		{ this->torrents_add(response, request); };

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
	
	rapidjson::Document document;
	document.SetObject();
	rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
	std::string http_header;
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
	
	rapidjson::Document document;
	document.SetObject();
	rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
	std::string http_header;
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
	// TODO - disabled for tests only. enable later.
	//if(!validate_authorization(request)) {
	//	respond_invalid_authorization(response, request);
	//	return;
	//}

	fs::path log_path;
	try {
		log_path = fs::path(config.get_config<std::string>("log.file_path"));
	}
	catch(config_key_error const &e) {
		LOG_ERROR << "Could not get log. Could not get config: " << e.what();
	}

	std::vector<char> buffer;
	bool result = file_to_buffer(buffer, log_path.string());
	
	rapidjson::Document document;
	document.SetObject();
	rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
	std::string http_header;
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
	
	rapidjson::Document document;
	document.SetObject();
	rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
	std::string http_header;
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
	
	rapidjson::Document document;
	document.SetObject();
	rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
	std::string http_header;
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

void RestAPI::torrents_get(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
	try {	
		const std::vector<std::shared_ptr<Torrent>> torrents = torrent_manager.get_torrents();
		rapidjson::Document document;
		document.SetObject();
		rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
		for(std::shared_ptr<Torrent> torrent : torrents) {
			lt::torrent_status status = torrent->get_handle().status();	
			rapidjson::Value object(rapidjson::kObjectType);
			rapidjson::Value temp_value;
			
			temp_value.SetString(status.name.c_str(), status.name.length(), allocator);
			object.AddMember("name", temp_value, allocator); 
			object.AddMember("down_rate", status.download_rate, allocator); 
			object.AddMember("up_rate", status.upload_rate, allocator); 
			object.AddMember("progress", status.progress, allocator); 
			object.AddMember("down_total", status.total_download, allocator); 
			object.AddMember("up_total", status.total_upload, allocator); 
			object.AddMember("seeds", status.num_seeds, allocator); 
			object.AddMember("peers", status.num_peers, allocator);
			/* info_hash: If this handle is to a torrent that hasn't loaded yet (for instance by being added) by a URL,
			   the returned value is undefined. */
			std::stringstream ss_info_hash;
			ss_info_hash << status.info_hash;
			std::string info_hash = ss_info_hash.str();
			temp_value.SetString(info_hash.c_str(), info_hash.length(), allocator);
			document.AddMember(temp_value, object, allocator);
		}

		rapidjson::StringBuffer string_buffer;
		rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(string_buffer);
		document.Accept(writer);
		std::string json = string_buffer.GetString();
	
		*response << "HTTP/1.1 200 OK\r\nContent-Length: " << json.length() << "\r\n\r\n"
				<< json;		
	}
	catch(const std::exception &e) {
		*response << "HTTP/1.1 400 Bad Request\r\nContent-Length: " << strlen(e.what()) << "\r\n\r\n"
					<< e.what();
		LOG_DEBUG << "HTTP Bad Request: " << e.what();
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
		
		rapidjson::Document document;
		document.SetObject();
		rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
		std::string http_header;
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

void add_torrents_from_request(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
    std::string buffer;
    buffer.resize(131072);

    std::string boundary;
    if(!getline(request->content, boundary)) {
      response->write(SimpleWeb::StatusCode::client_error_bad_request);
      return;
    }

    // go through all content parts
    while(true) {
      std::stringstream file; // std::stringstream is used as example output type
      std::string filename;

      auto header = SimpleWeb::HttpHeader::parse(request->content);
      auto header_it = header.find("Content-Disposition");
      if(header_it != header.end()) {
        auto content_disposition_attributes = SimpleWeb::HttpHeader::FieldValue::SemicolonSeparatedAttributes::parse(header_it->second);
        auto filename_it = content_disposition_attributes.find("filename");
        if(filename_it != content_disposition_attributes.end()) {
          filename = filename_it->second;

          bool add_newline_next = false; // there is an extra newline before content boundary, this avoids adding this extra newline to file
          // store file content in variable file
          while(true) {
            request->content.getline(&buffer[0], static_cast<std::streamsize>(buffer.size()));
            if(request->content.eof()) {
              response->write(SimpleWeb::StatusCode::client_error_bad_request);
              return;
            }
            auto size = request->content.gcount();

            if(size >= 2 && (static_cast<size_t>(size - 1) == boundary.size() || static_cast<size_t>(size - 1) == boundary.size() + 2) && // last boundary ends with: --
               std::strncmp(buffer.c_str(), boundary.c_str(), boundary.size() - 1 /*ignore \r*/) == 0 &&
               buffer[static_cast<size_t>(size) - 2] == '\r') // buffer must also include \r at end
              break;

            if(add_newline_next) {
              file.put('\n');
              add_newline_next = false;
            }

            if(!request->content.fail()) { // got line or section that ended with newline
              file.write(buffer.c_str(), size - 1); // size includes newline character, but buffer does not
              add_newline_next = true;
            }
            else
              file.write(buffer.c_str(), size);

            request->content.clear(); // clear stream state
          }

		std::ofstream ofs(filename);
		ofs << file.rdbuf();
		ofs.close();
        }
      }
      else { // no more parts
        response->write(); // Write empty success response
        return;
      }
    }
}

void RestAPI::torrents_add(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
	try {
		/*
		if(!validate_authorization(request)) {
			respond_invalid_authorization(response, request);
			return;
		}
		*/
		
		add_torrents_from_request(response, request);
		
	/*	
		// Generate .torrent file from content
		std::string name;
		do {
			name = random_string("1234567890", 20) + ".torrent";
		}
		while(boost::filesystem::exists(torrent_file_path + name));
		auto content = request->content.string();
		std::ofstream file;
		file.open(torrent_file_path + name, std::ofstream::out);
		file << content;
		file.close();

		bool success = torrent_manager.add_torrent_async(torrent_file_path + name, download_path);

		rapidjson::Document document;
		document.SetObject();
		rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
		std::string http_status;
		if(success) {
			document.AddMember("success", true, allocator);
			document.AddMember("status", "An attempt to add the torrent will be made asynchronously", allocator);
			http_status = "http/1.1 202 Accepted\r\n";
		}	
		else {
			document.AddMember("success", false, allocator);	
			document.AddMember("status", "Could not add torrent", allocator);
			http_status = "http/1.1 400 Bad Request\r\n";
		}

		rapidjson::StringBuffer string_buffer;
		rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(string_buffer);
		document.Accept(writer);
		std::string json = string_buffer.GetString();
		
		*response << http_status
				<< "Content-Length: " << json.length() << "\r\n\r\n"
				<< json;

				*/
	}
	catch(const std::exception &e) {
		*response << "HTTP/1.1 400 Bad Request\r\nContent-Length: " << strlen(e.what()) << "\r\n\r\n"
					<< e.what();	
		LOG_DEBUG << "HTTP Bad Request: " << e.what();
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
	std::string http_header = "Content-Length: " + std::to_string(json.length());

	*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n\r\n" << json;
	
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
	std::string http_header = "Content-Length: " + std::to_string(json.length());

	*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n\r\n" << json;
	
	LOG_DEBUG << "HTTP " << request->method << " " << request->path << " "  << http_status
		<< " to " << request->remote_endpoint_address() << " Message: " << message;
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
