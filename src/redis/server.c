// #include "server.h"
#include <stdio.h>
#include "server.h"
#include "client.h"
#include "dict/dict_plugins.h"
#include "debug/latte_debug.h"
#include "utils/utils.h"
#include "../shared/shared.h"
#include "object/object_manager.h"
#include "object_string_register.h"
/** 工具函数 **/
/**
 * 输入: filename 文件名（可能是相对路径或绝对路径）
 * 输出/返回: 返回绝对路径的SDS字符串，失败返回NULL
 * 功能: 将给定的文件名转换为绝对路径。如果文件名已经是绝对路径则直接处理。
 *       该函数不会标准化所有内容，只处理相对路径开头的一个或多个"../"的明显情况。
 */
sds get_absolute_path(char *filename) {
    char cwd[1024];                      // 当前工作目录缓冲区
    sds abspath;                         // 绝对路径
    sds relpath = sds_new(filename);     // 相对路径

    relpath = sds_trim(relpath," \r\n\t");  // 去除首尾空白字符
    if (relpath[0] == '/') return relpath;   // 如果路径已经是绝对路径，直接返回

    /* 如果是相对路径，将当前工作目录和相对路径拼接 */
    if (getcwd(cwd,sizeof(cwd)) == NULL) {
        sds_delete(relpath);
        return NULL;
    }
    abspath = sds_new(cwd);
    if (sds_len(abspath) && abspath[sds_len(abspath)-1] != '/')
        abspath = sds_cat(abspath,"/");   // 确保路径以"/"结尾

    /* 此时当前路径总是以"/"结尾，处理相对路径中明显的"../"情况。
     * 对于文件名中每个找到的"../"，我们将其移除，同时移除当前工作目录的最后一个元素，
     * 除非当前工作目录是根目录"/"。 */
    while (sds_len(relpath) >= 3 &&
           relpath[0] == '.' && relpath[1] == '.' && relpath[2] == '/')
    {
        sds_range(relpath,3,-1);         // 移除开头的"../"
        if (sds_len(abspath) > 1) {
            char *p = abspath + sds_len(abspath)-2;
            int trimlen = 1;

            while(*p != '/') {           // 向前查找上一级目录
                p--;
                trimlen++;
            }
            sds_range(abspath,0,-(trimlen+1));  // 移除上一级目录
        }
    }

    /* 最后将两部分拼接在一起 */
    abspath = sds_cat_sds(abspath,relpath);
    sds_delete(relpath);
    return abspath;
}


// void free_config(config_t* c) {
//     zfree(c);
// }


/** latte server module **/
// int startServer(struct latteServer* server) {
//     printf("start latte server %lld!!!!!\n", server->port);
//     return 1;
// }

/**
 * 输入: server 服务器实例指针
 * 输出/返回: 成功返回1
 * 功能: 停止Redis服务器
 */
int stopRedisServer(struct redis_server_t* server) {
    printf("stop latte server !!!!!\n");
    return 1;
}


/** 模块字典类型配置 - 大小写不敏感的字符串键字典 */
dict_func_t modules_dict_type = {
    dict_sds_case_hash,           // 大小写不敏感的哈希函数
    NULL,                         // 键复制函数（不使用）
    NULL,                         // 值复制函数（不使用）
    dict_sds_key_case_compare,    // 大小写不敏感的键比较函数
    dict_sds_destructor,          // 键销毁函数
    NULL,                         // 值销毁函数（不使用）
    NULL                          // 私有数据（不使用）
};

/**
 * 输入: rs Redis服务器实例指针
 * 输出/返回: 无返回值
 * 功能: 初始化Redis服务器的基本字段，包括协议配置、客户端管理、
 *       backlog缓冲区、命令管理器、模块系统和性能统计等
 */
void init_redis_server(struct redis_server_t* rs) {
    rs->proto_max_bulk_len = 1024 * 1024;     // 设置协议最大块长度为1MB
    rs->clients_to_close = list_new();         // 初始化待关闭客户端列表
    /* backlog: 最多 10000 条，总长不限制；可按需改为从 config 读取 */
    rs->backlog = backlog_new(10000, 0);       // 初始化命令回放缓冲区
    rs->command_manager = command_manager_new(); // 初始化命令管理器
    rs->modules = dict_new(&modules_dict_type);  // 初始化模块字典（大小写不敏感）
    rs->metric = metric_new(16);                // 初始化性能指标统计（16个槽位）
    rs->metric_stat_numcommands = 0;            // 初始化命令统计数量
    module_register_core_api(rs);               // 注册核心模块API
} 




/** Redis服务器启动相关函数 **/
/**
 * 输入: redis_server 服务器实例指针, argc 参数个数, argv 参数数组
 * 输出/返回: 成功返回1，失败返回0
 * 功能: 完整的Redis服务器启动流程，包括日志初始化、对象管理器初始化、
 *       配置加载、模块初始化、网络服务启动、数据库初始化和定时任务设置
 */
int start_redis_server(struct redis_server_t* redis_server, int argc, sds* argv) {
    log_module_init();                        // 初始化日志模块
    log_add_stdout(LATTE_LIB, LOG_INFO);     // 添加标准输出日志
    /* 初始化对象管理器并注册对象类型 */
    global_object_manager_init();             // 初始化全局对象管理器
    register_object_string_type();           // 注册字符串对象类型
    init_redis_server(redis_server);         // 初始化服务器基本配置
    init_shared_objects();                   // 初始化共享对象
    redis_server->exec_argc = argc;          // 保存执行参数个数
    redis_server->exec_argv = argv;          // 保存执行参数数组
    //argv[0] is exec file
    redis_server->executable = get_absolute_path(argv[0]);  // 获取可执行文件绝对路径
    redis_server->config_manager = config_manager_new();    // 创建配置管理器
    redis_server->config = server_config_new(redis_server->config_manager);  // 创建服务器配置

    //argv[1] maybe is config file
    int attribute_index = 1;                 // 配置文件参数索引
    if (argc > 1) {
        if (argv[1][0] != '-') {             // 如果第一个参数不是选项（不以-开头）
            redis_server->configfile = get_absolute_path(argv[1]);  // 获取配置文件绝对路径
            if (config_load_file(redis_server->config_manager, redis_server->configfile) == 0) {
                goto fail;                   // 配置文件加载失败
            }
            attribute_index++;               // 跳过配置文件参数
        }

        //add config attribute property
        if (config_load_argv(redis_server->config_manager, argv + attribute_index, argc - attribute_index) == 0) {
            goto fail;                       // 命令行参数解析失败
        }
    }

    log_set_level(LATTE_LIB, redis_server->config->log_level);  // 设置日志级别
    sds log_file = redis_server->config->logfile;
    if (log_file != NULL) {
        log_add_file(LATTE_LIB, log_file, redis_server->config->log_level);  // 添加文件日志输出
    }
    init_redis_modules(redis_server);        // 初始化Redis模块系统
    init_latte_server(&redis_server->server); // 初始化底层Latte服务器
    LATTE_LIB_LOG(LOG_INFO, "init redis server ");
    redis_server->server.maxclients = redis_server->config->max_clients;     // 设置最大客户端连接数
    redis_server->server.el = ae_event_loop_new(redis_server->config->event_loop_size);  // 创建事件循环
    redis_server->server.tcp_backlog = redis_server->config->tcp_backlog;    // 设置TCP backlog
    redis_server->server.port = redis_server->config->port;                  // 设置监听端口
    redis_server->server.maxclients = redis_server->config->max_clients;     // 再次设置最大客户端数（可能重复）
    redis_server->server.createClient = create_redis_client;                 // 设置客户端创建函数
    redis_server->server.freeClient = redis_client_delete;                   // 设置客户端释放函数
    redis_server->server.bind = redis_server->config->bind;                  // 设置绑定地址
    redis_server->hz = redis_server->config->hz;                             // 设置服务器运行频率
    redis_server->db_num = redis_server->config->db_num;                     // 设置数据库数量
    if (redis_server->config->use_async_io) {
        async_io_module_init();              // 初始化异步IO模块
        redis_server->server.use_async_io = true;  // 启用异步IO
    }
    LATTE_LIB_LOG(LOG_INFO, "init redis server config");
    init_redis_server_dbs(redis_server);     // 初始化数据库
    update_cache_time(redis_server);         // 更新缓存时间
    init_redis_server_crons(redis_server);   // 初始化定时任务
    redis_server->slowlog_manager = slowlog_manager_new(
        redis_server->config->slowlog_log_slower_than,
        redis_server->config->slowlog_max_len);      // 创建慢日志管理器
    start_latte_server(&redis_server->server);       // 启动底层服务器
    return 1;
fail:
    printf("start latte redis fail");        // 启动失败提示
    return 0;
}

/**
 * 输入: file 出错的文件名, line 出错的行号, msg 格式化错误消息, ... 可变参数
 * 输出/返回: 无返回值（程序会终止）
 * 功能: Redis panic处理函数，格式化错误消息并记录到日志中。
 *       当Redis遇到严重错误时调用，用于记录错误信息并终止程序执行。
 */
void _redis_panic(const char *file, int line, const char *msg, ...) {
    va_list ap;                              // 可变参数列表
    va_start(ap,msg);                        // 初始化可变参数列表
    char fmtmsg[256];                        // 格式化消息缓冲区
    vsnprintf(fmtmsg,sizeof(fmtmsg),msg,ap); // 格式化错误消息
    va_end(ap);                              // 清理可变参数列表
    LATTE_LIB_LOG(LOG_ERROR, fmtmsg);       // 记录错误日志
}



