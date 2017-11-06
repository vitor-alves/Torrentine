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
#include "lib/plog/Log.h"
#include "lib/rapidjson/document.h"
#include "lib/rapidjson/prettywriter.h"
#include "lib/rapidjson/writer.h"
#include "lib/rapidjson/stringbuffer.h"
#include <typeinfo>

RestAPI::RestAPI(ConfigManager config, TorrentManager& torrent_manager) : torrent_manager(torrent_manager) {
	std::string api_port;
	try {
		torrent_file_path = config.get_config("directory.torrent_file_path");
		download_path = config.get_config("directory.download_path"); 
		api_port = config.get_config("api.port");
	}
	catch(const boost::property_tree::ptree_error &e) {
		LOG_ERROR << e.what();
	}
	std::stringstream ss(api_port);
	ss >> server.config.port;
	if(ss.fail()) {
		LOG_ERROR << "Problem with api port " << api_port <<" from config";
	}	
	define_resources();
}		

RestAPI::~RestAPI() {
	stop_server();
}

void RestAPI::start_server() {
	server_thread = new std::thread( [this](){ server.start(); } );

	server.on_error = [](std::shared_ptr<HttpServer::Request> request, const SimpleWeb::error_code & ec) {
		LOG_ERROR << ec.message();
	};
}

void RestAPI::stop_server() {
	server.~Server();
	delete server_thread;
}

// WARNING: do not add or remove resources after start() is called
void RestAPI::define_resources() {
	// /torrents/<id>/stop
	server.resource["^/torrents/(?:([0-9,]*)/|)stop$"]["GET"] =
	      	[&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) 
		{ this->torrents_stop(response, request); };

	server.resource["^/torrents$"]["GET"] =
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

void RestAPI::torrents_stop(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
	try {
		std::vector<unsigned long int> ids = split_string_ulong(request->path_match[1], ',');
		
		// TODO - finish this implementation. Use the ids vector.

		SimpleWeb::CaseInsensitiveMultimap query = request->parse_query_string();
		if( query.find("id") == query.end() || query.find("force_stop") == query.end() )
			throw std::invalid_argument("Invalid parameters");
		
		const unsigned long int id = stoul(query.find("id")->second);
		bool force_stop;
		std::istringstream(query.find("force_stop")->second) >> std::boolalpha >> force_stop;
		bool result = torrent_manager.stop_torrent(id, force_stop);
	
		rapidjson::Document document;
		document.SetObject();
		rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
		std::string http_status;
		if(result == true) {	
			document.AddMember("status", "An attempt to pause the torrent will be made asynchronously", allocator);
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
		LOG_ERROR << "HTTP Bad Request: " << e.what();
	}

}

void RestAPI::torrents_get(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
	try {	
		const std::vector<Torrent*> torrents = torrent_manager.get_torrents();
		rapidjson::Document document;
		document.SetObject();
		rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
		for(Torrent* torrent : torrents) {
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
		LOG_ERROR << "HTTP Bad Request: " << e.what();
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
		LOG_ERROR << "HTTP Bad Request: " << e.what();
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
		LOG_ERROR << "HTTP Bad Request: " << e.what();
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
