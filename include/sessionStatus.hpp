#ifndef SESSION_STATUS_H    
#define SESSION_STATUS_H

// TODO - there are others field that COULD be here. Read the docs on session_stats from libtorrent  
struct SessionStatus {
        bool has_incoming_connections = false;
        long upload_rate = 0;
        long download_rate = 0;
	long total_download = 0;
        long total_upload = 0;
        long total_payload_download = 0;
        long total_payload_upload = 0;
        long payload_download_rate = 0;
        long payload_upload_rate = 0;
	long ip_overhead_upload_rate = 0;
        long ip_overhead_download_rate = 0;
        long ip_overhead_upload = 0;
        long ip_overhead_download = 0;
	long dht_upload_rate = 0;
        long dht_download_rate = 0;
        long dht_nodes = 0;
        long dht_upload = 0;
        long dht_download = 0;
	long tracker_upload_rate = 0;
        long tracker_download_rate = 0;
	long tracker_upload = 0;
        long tracker_download = 0;
	long num_peers_connected = 0;
	long num_peers_half_open = 0;
	long total_peers_connections = 0;
};

#endif
