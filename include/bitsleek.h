#include "torrentManager.h"
#include "config.h"
#include "plog/Log.h"

std::unordered_map<std::string, plog::Severity> map_log_severity({{"none",plog::Severity::none},
		       			{"fatal",plog::Severity::fatal},
					{"error",plog::Severity::error},
					{"warning",plog::Severity::warning},
					{"info",plog::Severity::info},
					{"debug",plog::Severity::debug},
					{"verbose",plog::Severity::verbose}});

void initialize_log(ConfigManager &config);
void add_test_torrents(TorrentManager &torrent_manager, ConfigManager &config);