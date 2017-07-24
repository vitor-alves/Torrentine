// Added for json
#define BOOST_SPIRIT_THREADSAFE
#include <boost/property_tree/json_parser.hpp>
#include <boost/filesystem.hpp>
// Added for default resource
#include <algorithm>
#include <boost/filesystem.hpp>
#include <fstream>
#include <vector>
#ifdef HAVE_OPENSSL
#include "crypto.hpp"
#endif
// local headers
#include "restAPI.h"
#include "torrentManager.h"

using namespace std;
using namespace boost::property_tree;

RestAPI::RestAPI(int port, TorrentManager& torrent_manager) : torrent_manager(torrent_manager) {
	server.config.port = port;
	define_resources();
}

RestAPI::~RestAPI() {
	stop_server();
}

void RestAPI::start_server() {
	server_thread = new thread( [this](){ server.start(); } );
	// TODO - need to wait for thread to finish ? server_thread.join();

	server.on_error = [](shared_ptr<HttpServer::Request> /*request*/, const SimpleWeb::error_code & /*ec*/) {
		// TODO - Handle errors here
		//		when server cant bind to port where does the error occurs ? treat it.
	};
}

void RestAPI::stop_server() {
	// TODO - how safe is this ? If the thread gets killed in the middle of certain operations
	// 			things may get corrupted.
	delete server_thread;
}

void RestAPI::define_resources() {

	/* GET request resource - returns json containing all torrents in torrent_handle_vector */
	server.resource["^/torrent$"]["GET"] = [&](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
		std::vector<lt::torrent_handle>& torrent_handle_vector = torrent_manager.get_torrent_handle_vector();

		/* Construct a property tree containing information from all torrents in torrent_handle_vector.
			Used to write a JSON that is sent as response. */
		ptree tree;
		for(lt::torrent_handle handle : torrent_handle_vector) {
			// TODO - child is a exists only inside the "for", yet I am passing using the tree with the childs outside the for.
			//			When add_child is called what happens to the child? How safe is this ?

			// TODO - assure those values exist for EVERY handle. Read the docs to understand.
			ptree child;
			child.put("name", handle.status().name);
			child.put("down_rate", handle.status().download_rate);
			child.put("up_rate", handle.status().upload_rate);
			child.put("progress", handle.status().progress);
			child.put("down_total", handle.status().total_download);
			child.put("up_total", handle.status().total_upload);
			child.put("seeds", handle.status().num_seeds);
			child.put("peers", handle.status().num_peers);

			/* TODO - be careful here! fix this later! info_hash() returns the info-hash of the torrent.
			  If this handle is to a torrent that hasn't loaded yet (for instance by being added) by a URL,
			   the returned value is undefined. */
			std::stringstream str_stream;
			str_stream << handle.status().info_hash;
			
			tree.add_child(str_stream.str(), child);
		}

		std::stringstream str_stream;
		write_json(str_stream, tree);

		string json = str_stream.str();
		
		*response << "HTTP/1.1 200 OK\r\nContent-Length: " << json.length() << "\r\n\r\n"
					<< json;
	};

	/* POST request resource - receive json in format 
	{
	"firstName": "John",
	"lastName": "Smithff",
	"age": 25
	} 
	and returns data it contains */
	server.resource["^/json$"]["POST"] = [&](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
		try {
			ptree pt;
			read_json(request->content, pt);

			auto name = pt.get<string>("firstName") + " " + pt.get<string>("lastName");

			*response << "HTTP/1.1 200 OK\r\n"
						<< "Content-Length: " << name.length() << "\r\n\r\n"
						<< name;
		}
		catch(const exception &e) {
			*response << "HTTP/1.1 400 Bad Request\r\nContent-Length: " << strlen(e.what()) << "\r\n\r\n"
						<< e.what();
		}
	};

	/* Default webserver used to access webUI. http://localhost:port/. */
	server.default_resource["GET"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
		try {
			auto web_root_path = boost::filesystem::canonical("webUI");
			auto path = boost::filesystem::canonical(web_root_path / request->path);
			// Check if path is within web_root_path
			if(distance(web_root_path.begin(), web_root_path.end()) > distance(path.begin(), path.end()) ||
			!equal(web_root_path.begin(), web_root_path.end(), path.begin()))
				throw invalid_argument("path must be within root path");
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

			auto ifs = make_shared<ifstream>();
			ifs->open(path.string(), ifstream::in | ios::binary | ios::ate);

			if(*ifs) {
				auto length = ifs->tellg();
				ifs->seekg(0, ios::beg);

				header.emplace("Content-Length", to_string(length));
				response->write(header);

				// Trick to define a recursive function within this scope (for your convenience)
				class FileServer {
				public:
					static void read_and_send(const shared_ptr<HttpServer::Response> &response, const shared_ptr<ifstream> &ifs) {
						// Read and send 128 KB at a time
						static vector<char> buffer(131072); // Safe when server is running on one thread
						streamsize read_length;
						if((read_length = ifs->read(&buffer[0], buffer.size()).gcount()) > 0) {
							response->write(&buffer[0], read_length);
							if(read_length == static_cast<streamsize>(buffer.size())) {
								response->send([response, ifs](const SimpleWeb::error_code &ec) {
									if(!ec)
										read_and_send(response, ifs);
									else
										cerr << "Connection interrupted" << endl;
								});
							}
						}
					}
				};
				FileServer::read_and_send(response, ifs);
			}
			else
				throw invalid_argument("could not read file");
			}
		catch(const exception &e) {
			response->write(SimpleWeb::StatusCode::client_error_bad_request, "Could not open path " + request->path + ": " + e.what());
		}
	};
}