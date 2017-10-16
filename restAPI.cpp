#define BOOST_SPIRIT_THREADSAFE
#include <boost/property_tree/json_parser.hpp>
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

RestAPI::RestAPI(ConfigManager config, TorrentManager& torrent_manager) : torrent_manager(torrent_manager) {
	server.config.port = stoi(config.get_config("api.port"));
	
	try {
		torrent_file_path = config.get_config("directory.torrent_file_path");
		download_path = config.get_config("directory.download_path"); }
	catch(boost::property_tree::ptree_error &e) {
		std::cerr << e.what() << std::endl; }	// TODO - Treat/Log this
	define_resources();
}		

RestAPI::~RestAPI() {
	stop_server();
}

void RestAPI::start_server() {
	server_thread = new std::thread( [this](){ server.start(); } );

	server.on_error = [](std::shared_ptr<HttpServer::Request>, const SimpleWeb::error_code & ) {
		// TODO - log this
	};
}

void RestAPI::stop_server() {
	// TODO - how safe is this ? If the thread gets killed in the middle of certain operations
	// 	things may get corrupted.
	server.~Server(); // TODO - I dont know if this is enough because I need to see what the destructor does
	delete server_thread;
} 

/* Define HTTP resources */
void RestAPI::define_resources() {

	/* GET request resource - returns json containing all torrents */
	server.resource["^/torrent$"]["GET"] = [&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
		try { 
			const std::vector<Torrent*> torrents = torrent_manager.get_torrents();

			/* Construct a property tree containing information from all torrents .
			Used to write a JSON that is sent as response. */
			boost::property_tree::ptree tree;
			for(Torrent* torrent : torrents) {
				boost::property_tree::ptree child;
				lt::torrent_status status = torrent->get_handle().status();
				
				child.put("name", status.name);
				child.put("down_rate", status.download_rate);
				child.put("up_rate", status.upload_rate);
				child.put("progress", status.progress);
				child.put("down_total",status.total_download);
				child.put("up_total", status.total_upload);
				child.put("seeds", status.num_seeds);
				child.put("peers", status.num_peers);
				/* TODO - be careful here! fix this later! info_hash() returns the info-hash of the torrent.
				  If this handle is to a torrent that hasn't loaded yet (for instance by being added) by a URL,
				   the returned value is undefined. */
				std::stringstream ss_info_hash;
				ss_info_hash << status.info_hash;
				tree.add_child(ss_info_hash.str(), child);
			}
	
			std::stringstream str_stream;
			boost::property_tree::write_json(str_stream, tree);

			std::string json = str_stream.str();
		
			*response << "HTTP/1.1 200 OK\r\nContent-Length: " << json.length() << "\r\n\r\n"
					<< json;		
		}
		catch(const std::exception &e) {
			*response << "HTTP/1.1 400 Bad Request\r\nContent-Length: " << strlen(e.what()) << "\r\n\r\n"
						<< e.what();
		}
	};
	
	/* TODO - change this to accept multiple torrent ids/remove_data to remove them all at once */
	/* TODO - This is what I am doing. Document this somehow. Im not creating a method for the client to verify if the torrent was
			 in fact deleted for now. Maybe I will in the future.
			 https://www.safaribooksonline.com/library/view/restful-web-services/9780596809140/ch01s10.html */
	server.resource["^/torrent/remove$"]["DELETE"] = [&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
		try {
			
			SimpleWeb::CaseInsensitiveMultimap query = request->parse_query_string();
			 /* Store value in variable. Does any other function needs this too? Take a look
			 * Maybe I need to verify is the values are valid also. Its possible to send the parameters with any character/number */

			/* If any of the parameters do not exist, send an error response */	
			if( query.find("id") == query.end() || query.find("remove_data") == query.end() )
				throw std::invalid_argument("Invalid parameters");
			
			/* Remove torrent */
			const unsigned long int id = stoul(query.find("id")->second);
			bool remove_data;
		  	std::istringstream(query.find("remove_data")->second) >> std::boolalpha >> remove_data;
			bool result = torrent_manager.remove_torrent(id, remove_data);
				
			std::string json;
			if(result == true) {
				json = "{\"status\":\"An attempt to remove the torrent will be made\"}";
				*response << "HTTP/1.1 202 Accepted\r\n"
						<< "Content-Length: " << json.length() << "\r\n\r\n"
						<< json;
			}
			else {
				std::stringstream ss;
				ss << "{\"status\":\"Could not find a torrent with id: " << id << "\"}";
				json = ss.str();
				*response << "HTTP/1.1 404 Not Found\r\n"
						<< "Content-Length: " << json.length() << "\r\n\r\n"
						<< json;
			}	
		}
		catch(const std::exception &e) {
			*response << "HTTP/1.1 400 Bad Request\r\nContent-Length: " << strlen(e.what()) << "\r\n\r\n"
						<< e.what();
		}		
	
	};

	/* POST request resource - adds new torrent from .torrent file */
	// TODO - what if torrent file (or POST content) is not valid?
	// Maybe there is a way to receive multiple files
	server.resource["^/torrent/add_file$"]["POST"] = [&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
		try {
			/* Generate random name for .torrent file */
			std::string name;
			do {
				name = randomString("1234567890", 20) + ".torrent";
			}
			while(boost::filesystem::exists(torrent_file_path + name));

			/* Write .torrent file */
			auto content = request->content.string();
			std::ofstream file;
			// TODO - what if there is no permission to write file ?
			file.open(torrent_file_path + name, std::ofstream::out);
			file << content;
			file.close();

			/* Add torrent */
			torrent_manager.add_torrent_async(torrent_file_path + name, download_path);

			std::string json = "{file:\""+name+"\",success:\"true\"}";

			*response << "HTTP/1.1 200 OK\r\n"
						<< "Content-Length: " << json.length() << "\r\n\r\n"
						<< json;
		}
		catch(const std::exception &e) {
			*response << "HTTP/1.1 400 Bad Request\r\nContent-Length: " << strlen(e.what()) << "\r\n\r\n"
						<< e.what();
		}
	};

	/* Default webserver used to access webUI. http://localhost:port/. */
	server.default_resource["GET"] = [](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
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
		}
	};
}
