#include "config.h"
#include "log/log.h"
#include "zmalloc/zmalloc.h"
#include "utils/utils.h"

#define DEFAULT_PORT 6379
#define DEFAULT_TCP_BACKLOG 512
// config_rule_t port_rule = LL_CONFIG_INIT(DEFAULT_PORT);
// config_rule_t bind_rule = SDS_ARRAY_CONFIG_INIT("* -::*");
// config_rule_t tcp_backlog_rule = LL_CONFIG_INIT(DEFAULT_TCP_BACKLOG);
// config_rule_t logfile_rule = SDS_CONFIG_INIT("");
// config_rule_t max_client_rule =  LL_CONFIG_INIT(300);
// config_rule_t use_async_io_rule = LL_CONFIG_INIT(0);
// config_rule_t log_level_rule = LL_CONFIG_INIT(LOG_INFO);
// config_rule_t event_loop_size_rule = LL_CONFIG_INIT(1024);
// config_rule_t hz_rule = LL_CONFIG_INIT(10);
// config_rule_t db_num_rule = LL_CONFIG_INIT(16);

// config_rule_t load_module_rule = EVENT_CONFIG_INIT();

config_enum_t log_level_enum_list[] = {
    { "trace", LOG_TRACE},
    { "debug", LOG_DEBUG},
    { "info", LOG_INFO},
    { "warn", LOG_WARN},
    { "error", LOG_ERROR},
    { "fatal", LOG_FATAL},
    { NULL,  0},
};

server_config_t* server_config_new(config_manager_t* manager) { 
    config_manager_t* config_manager = config_manager_new();
    server_config_t* config = zmalloc(sizeof(server_config_t));
    config_add_rule(config_manager, "bind", config_rule_new_sds_array_rule(0, &config->bind, NULL, -1, sds_new("* -::*")));
    config_add_rule(config_manager, "port", config_rule_new_numeric_rule(0, &config->port, 0, 65535, NULL, DEFAULT_PORT));
    config_add_rule(config_manager, "tcp-backlog", config_rule_new_numeric_rule(0, &config->tcp_backlog, 0, 65535, NULL, DEFAULT_TCP_BACKLOG));
    config_add_rule(config_manager, "log-file", config_rule_new_sds_rule(0, &config->logfile, NULL, NULL));
    config_add_rule(config_manager, "log-level", config_rule_new_enum_rule(0, &config->log_level, log_level_enum_list, NULL, "info"));    
    config_add_rule(config_manager, "max-clients", config_rule_new_numeric_rule(0, &config->max_clients, 0, 65535, NULL, 10000));
    config_add_rule(config_manager, "use-async-io", config_rule_new_numeric_rule(0, &config->use_async_io, 0, 1, NULL, 0));
    config_add_rule(config_manager, "event-loop-size", config_rule_new_numeric_rule(0, &config->event_loop_size, 0, 65535, NULL, 1024));
    config_add_rule(config_manager, "hz", config_rule_new_numeric_rule(0, &config->hz, 0, 65535, NULL, 10));
    config_add_rule(config_manager, "db-num", config_rule_new_numeric_rule(0, &config->db_num, 0, 65535, NULL, 16));
    // config_add_rule(config_manager, "load-module", config_rule_new_sds_rule(0, &config->load_modules, NULL, sds_new("")));
    
    latte_assert_with_info(config_init_all_data(config_manager) > 0, "config_init_all_data failed");
    return config;
}

void server_config_delete(server_config_t* config) {
    zfree(config);
}