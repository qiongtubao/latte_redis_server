#include "config/config.h"

#define DEFAULT_PORT 6379
#define DEFAULT_TCP_BACKLOG 512
config_rule_t port_rule = LL_CONFIG_INIT(DEFAULT_PORT);
config_rule_t bind_rule = SDS_ARRAY_CONFIG_INIT("* -::*");
config_rule_t tcp_backlog_rule = LL_CONFIG_INIT(DEFAULT_TCP_BACKLOG);
config_rule_t logfile_rule = SDS_CONFIG_INIT("redis.log");
config_rule_t max_client_rule =  LL_CONFIG_INIT(300);



config_manager_t* create_server_config() {
    config_manager_t* config_manager = config_manager_new();
    config_register_rule(config_manager, sds_new("port"), &port_rule);
    config_register_rule(config_manager, sds_new("bind"), &bind_rule);
    config_register_rule(config_manager, sds_new("tcp-backlog"), &tcp_backlog_rule);
    config_register_rule(config_manager, sds_new("log-file"), &logfile_rule);
    config_register_rule(config_manager, sds_new("max-clients"), &max_client_rule);
    return config_manager;
}