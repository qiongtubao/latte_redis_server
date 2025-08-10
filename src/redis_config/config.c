#include "config.h"
#include "log/log.h"

#define DEFAULT_PORT 6379
#define DEFAULT_TCP_BACKLOG 512
config_rule_t port_rule = LL_CONFIG_INIT(DEFAULT_PORT);
config_rule_t bind_rule = SDS_ARRAY_CONFIG_INIT("* -::*");
config_rule_t tcp_backlog_rule = LL_CONFIG_INIT(DEFAULT_TCP_BACKLOG);
config_rule_t logfile_rule = SDS_CONFIG_INIT("");
config_rule_t max_client_rule =  LL_CONFIG_INIT(300);
config_rule_t use_async_io_rule = LL_CONFIG_INIT(0);
config_rule_t log_level_rule = LL_CONFIG_INIT(LOG_INFO);
config_rule_t event_loop_size_rule = LL_CONFIG_INIT(1024);
config_rule_t hz_rule = LL_CONFIG_INIT(10);
config_rule_t db_num_rule = LL_CONFIG_INIT(16);

config_manager_t* create_server_config() {
    config_manager_t* config_manager = config_manager_new();
    config_register_rule(config_manager, sds_new("port"), &port_rule);
    config_register_rule(config_manager, sds_new("bind"), &bind_rule);
    config_register_rule(config_manager, sds_new("tcp-backlog"), &tcp_backlog_rule);
    config_register_rule(config_manager, sds_new("log-file"), &logfile_rule);
    config_register_rule(config_manager, sds_new("log-level"), &log_level_rule);
    config_register_rule(config_manager, sds_new("max-clients"), &max_client_rule);
    config_register_rule(config_manager, sds_new("use-async-io"), &use_async_io_rule);
    config_register_rule(config_manager, sds_new("event-loop-size"), &event_loop_size_rule);
    config_register_rule(config_manager, sds_new("hz"), &hz_rule);
    config_register_rule(config_manager, sds_new("db-num"), &db_num_rule);
    return config_manager;
}