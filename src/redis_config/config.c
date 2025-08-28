#include "config.h"
#include "log/log.h"
#include "zmalloc/zmalloc.h"
#include "utils/utils.h"
#include "../redis/module.h"

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





void config_load_modules_add(void* data_ctx, void* new_value) {
    vector_t* new_modules = (vector_t*)new_value;
    vector_t* old_modules = *(vector_t**)data_ctx;
    if (old_modules == NULL) {
        *(vector_t**)data_ctx = new_modules;
        return;
    }
    
    for (int i = 0; i < vector_size(new_modules); i++) {
        vector_push(old_modules, vector_pop(new_modules));
    }
    vector_delete(new_modules);

}

void* config_load_modules_get(void* data_ctx) {
    return (void*)*(vector_t**)data_ctx;
}

void* config_load_modules_load(config_rule_t* rule, char** argv, int argc, char** error) {
   vector_t* modules = vector_new();
   LATTE_LIB_LOG(LOG_INFO, "load module %s", argv[0]);
   module_entry_t* entry = module_entry_new(sds_new(argv[0]), argc -1, argv + 1);
   vector_push(modules, entry);
   return (void*)modules;
}

int config_load_modules_cmp(void* data_ctx, void* new_value) {
    latte_assert_with_info(0, "config_load_modules_cmp not implemented");
    return 1;
}

int config_load_modules_is_valid(void* data_ctx, void* new_value) {
    latte_assert_with_info(0, "config_load_modules_is_valid not implemented");
    // vector_t* modules = *(vector_t**)data_ctx;
    // vector_t* new_modules = (vector_t*)new_value;
    // for (int i = 0; i < vector_size(new_modules); i++) {
    //     module_entry_t* entry = vector_get(new_modules, i);
    //     if (sds_len(entry->path) == 0) {
    //         return 0;
    //     }
    //     //file exists
    //     if (!file_exists(entry->path)) {
    //         return 0;
    //     }
    // }
    return 1;
}

sds config_load_modules_to_sds(void* data_ctx, char* key, void* value) {
    latte_assert_with_info(0, "config_load_modules_to_sds not implemented");
    return NULL;
}

void config_module_entry_delete(void* data) {
    module_entry_t* entry = (module_entry_t*)data;
    module_entry_delete(entry);
}   


server_config_t* server_config_new(config_manager_t* config_manager) { 
    server_config_t* config = zmalloc(sizeof(server_config_t));
    config->bind = NULL;
    config->logfile = NULL;
    config->load_modules = NULL;
    config->port = DEFAULT_PORT;
    config->tcp_backlog = DEFAULT_TCP_BACKLOG;
    config->log_level = LOG_INFO;
    config->max_clients = 10000;
    config->use_async_io = false;
    config->event_loop_size = 1024;
    config->hz = 10;
    config->db_num = 16;
    config->slowlog_log_slower_than = 10000;
    config->slowlog_max_len = 128;

    config_add_rule(config_manager, "bind", config_rule_new_sds_array_rule(0, &config->bind, NULL, -1, sds_new("* -::*")));
    config_add_rule(config_manager, "port", config_rule_new_numeric_rule(0, &config->port, 0, 65535, NULL, DEFAULT_PORT));
    config_add_rule(config_manager, "tcp-backlog", config_rule_new_numeric_rule(0, &config->tcp_backlog, 0, 65535, NULL, DEFAULT_TCP_BACKLOG));
    config_add_rule(config_manager, "log-file", config_rule_new_sds_rule(0, &config->logfile, NULL, NULL));
    config_add_rule(config_manager, "log-level", config_rule_new_enum_rule(0, &config->log_level, log_level_enum_list, NULL, "info"));    
    config_add_rule(config_manager, "max-clients", config_rule_new_numeric_rule(0, &config->max_clients, 0, 65535, NULL, 10000));
    config_add_rule(config_manager, "use-async-io", config_rule_new_numeric_rule(0, &config->use_async_io, 0, 1, NULL, false));
    config_add_rule(config_manager, "event-loop-size", config_rule_new_numeric_rule(0, &config->event_loop_size, 0, 65535, NULL, 1024));
    config_add_rule(config_manager, "hz", config_rule_new_numeric_rule(0, &config->hz, 0, 65535, NULL, 10));
    config_add_rule(config_manager, "db-num", config_rule_new_numeric_rule(0, &config->db_num, 0, 65535, NULL, 16));
    config_add_rule(config_manager, "load-module", config_rule_new(
        CONFIG_FLAG_DISABLE_SAVE,
        &config->load_modules,
        config_load_modules_add,
        config_load_modules_get,
        NULL,
        config_load_modules_load,
        config_load_modules_cmp,
        config_load_modules_is_valid,
        config_load_modules_to_sds,
        NULL,
        NULL,
        config_module_entry_delete,
        NULL
    ));
    config_add_rule(config_manager, "slowlog-log-slower-than", 
        config_rule_new_numeric_rule(0, &config->slowlog_log_slower_than, 0, INT64_MAX, NULL, 10000));
    config_add_rule(config_manager, "slowlog-max-len", 
        config_rule_new_numeric_rule(0, &config->slowlog_max_len, 0, INT64_MAX, NULL, 128));
    latte_assert_with_info(config_init_all_data(config_manager) == 13, "config_init_all_data failed");
    return config;
}

void server_config_delete(server_config_t* config) {
    zfree(config);
}