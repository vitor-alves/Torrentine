void get_all_peers_info(lt::torrent_handle &handle) {
    std::vector<lt::peer_info> peer_info;
    handle.get_peer_info(peer_info);

    std::cout << "### PEERS LIST ###" << std::endl;
    for(lt::peer_info peer : peer_info ) {
        std::cout << "IP: " << peer.ip.address() << " Down Speed: " << peer.down_speed <<
                  " Up Speed: " << peer.up_speed << std::endl;
    }

}