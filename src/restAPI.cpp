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

RestAPI::RestAPI(ConfigManager &config, TorrentManager &torrent_manager) : torrent_manager(torrent_manager) {
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
	server.resource["^/session/torrents/(?:([0-9,]*)/|)stop$"]["PATCH"] =
	      	[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) 
		{ this->torrents_stop(response, request); };
	
	/* /session/torrents/<id> - GET */
	server.resource["^/session/torrents(?:/([0-9,]+)|)$"]["GET"] =
		[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) 
		{ this->torrents_get(response, request); };
	
	server.resource["^/torrents$"]["DELETE"] =
		[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request)
		{ this->torrents_delete(response, request); };

	server.resource["^/torrents/add_file$"]["POST"] =
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

// TODO - specify necessary headers in request and necessary headers in response
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
	if(result == 0) {	
		rapidjson::Value data(rapidjson::kArrayType);
		rapidjson::Value d(rapidjson::kObjectType);
		char const *message = "An attempt to stop the torrents will be made asynchronously";
		d.AddMember("message", rapidjson::StringRef(message), allocator);
		data.PushBack(d, allocator);
		document.AddMember("data", data, allocator);
		
		std::string json = stringfy_document(document);
		
		std::string http_status = "202 Accepted";
		std::string http_header = "Content-Length: " + std::to_string(json.length());
		
		LOG_DEBUG << "HTTP " << request->method << " " << request->path << " "  << http_status
			<< " to " << request->remote_endpoint_address << " Message: " << message;
		
		*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n\r\n" << json;
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
		
		std::string http_status = "404 Not Found";
		std::string http_header = "Content-Length: " + std::to_string(json.length());
	
		LOG_DEBUG << "HTTP " << request->method << " " << request->path << " "  << http_status
			<< " to " << request->remote_endpoint_address << " Message: " << message;

		*response << "HTTP/1.1 " << http_status << "\r\n" << http_header << "\r\n\r\n" << json;
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
			/* TODO - be careful here! fix this later! info_hash() returns the info-hash of the torrent.
			  If this handle is to a torrent that hasn't loaded yet (for instance by being added) by a URL,
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
	try {
		
		SimpleWeb::CaseInsensitiveMultimap query = request->parse_query_string();
		if( query.find("id") == query.end() || query.find("remove_data") == query.end() )
			throw std::invalid_argument("Invalid or missing parameters in request");
		
		const unsigned long int id = stoul(query.find("id")->second);
		bool remove_data;
		std::istringstream(query.find("remove_data")->second) >> std::boolalpha >> remove_data;
		bool result = torrent_manager.remove_torrent(id, remove_data);
		
		rapidjson::Document document;
		document.SetObject();
		rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
		std::string http_status;
		if(result == true) {	
			document.AddMember("status", "An attempt to remove the torrent will be made asynchronously", allocator);
			http_status = "HTTP/1.1 202 Accepted\r\n";
		}
		else {	
			document.AddMember("status", "Could not find torrent", allocator);
			http_status = "HTTP/1.1 404 Not Found\r\n";
		}
		document.AddMember("id", id, allocator);

		rapidjson::StringBuffer string_buffer;
		rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(string_buffer);
		document.Accept(writer);
		std::string json = string_buffer.GetString();

		*response << http_status
				<< "Content-Length: " << json.length() << "\r\n\r\n"
				<< json;
	}
	catch(const std::exception &e) {
		*response << "HTTP/1.1 400 Bad Request\r\nContent-Length: " << strlen(e.what()) << "\r\n\r\n"
					<< e.what();
		LOG_DEBUG << "HTTP Bad Request: " << e.what();
	}		
}

void RestAPI::torrents_add(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
	try {
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
		<< " to " << request->remote_endpoint_address << " Message: " << message;
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
		<< " to " << request->remote_endpoint_address << " Message: " << message;
}

/* TODO - create logic */
bool RestAPI::is_parameter_format_valid() {
	return true;
}

bool RestAPI::validate_parameter(SimpleWeb::CaseInsensitiveMultimap const &query, SimpleWeb::CaseInsensitiveMultimap::iterator const it_parameter, int const parameter_format, std::vector<std::string> const &allowed_values) {
	if(it_parameter == query.end()) {
		return false;
	}
	if(!is_parameter_format_valid()) {
		  return false;
	}
	if(allowed_values.empty()) {
		return true;
	}
	for(std::string value : allowed_values) {
		if(it_parameter->second == value) {
			return true;
		}
	}
	return false;
}


  static int callback(void *NotUsed, int argc, char **argv, char **azColName){
    int i;
    for(i=0; i<argc; i++){
      printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
  }
  printf("\n");
  return 0;
}


// TODO - create logic.
bool RestAPI::is_authorization_valid(std::string authorization_base64) {
	sqlite3 *db;
	
	fs::path users_db_path = "state/database/chinook.db"; // TODO - get config

	int ret = sqlite3_open(users_db_path.string().c_str(), &db);
	if(ret != SQLITE_OK) {
		LOG_ERROR << "Could not open database " << users_db_path.string() << ". sqlite3 msg: " << sqlite3_errmsg(db); 
		sqlite3_close(db);
		return false;
	}

	char *error_msg = 0;
	ret = sqlite3_exec(db, "select * from artists", callback, 0, &error_msg);
	if(ret != SQLITE_OK) {
		LOG_ERROR << "SQL error: " << error_msg << ". sqlite3 code: " << ret;
		sqlite3_free(error_msg);
	}

	sqlite3_close(db);
	
	return true;
}
