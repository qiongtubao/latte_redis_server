#include "command_manager.h"
#include "../redis/module.h"

/**
 * MODULE命令实现：Redis模块管理功能
 * 输入: c - 客户端连接对象
 * 用法: MODULE <subcommand> [args...]
 * 功能: 根据子命令分发到相应的模块管理功能
 *
 * 支持的子命令：
 * - MODULE HELP: 显示模块命令帮助信息
 * - MODULE LOAD <path> [args...]: 加载指定路径的模块
 * - MODULE UNLOAD <name>: 卸载指定名称的模块
 * - MODULE LIST: 列出当前加载的所有模块
 */
void module_command(redis_client_t* c) {
    char* subcmd = c->argv[1]->ptr;  /* 获取子命令字符串 */

    /* 根据子命令进行大小写不敏感的分发 */
    if (c->argc == 2 && !strcasecmp(subcmd, "help")) {
        /* MODULE HELP：显示模块系统的帮助信息 */
        module_help_command(c);
    } else if (!strcasecmp(subcmd,"load") && c->argc >= 3) {
        /* MODULE LOAD <path> [args...]：动态加载模块
         * 需要至少3个参数：MODULE、LOAD、模块路径 */
        module_load_command(c);
    } else if (!strcasecmp(subcmd,"unload") && c->argc >= 3) {
        /* MODULE UNLOAD <name>：卸载已加载的模块
         * 需要至少3个参数：MODULE、UNLOAD、模块名称 */
        module_unload_command(c);
    } else if (!strcasecmp(subcmd,"list") && c->argc >= 3) {
        /* MODULE LIST：列出所有已加载的模块及其信息
         * 注意：此处argc >= 3的条件可能有误，LIST通常不需要额外参数 */
        module_list_command(c);
    } else {
        /* 未知子命令或参数不足，应该返回语法错误
         * TODO: 取消注释以启用错误处理 */
        // add_reply_subcommand_syntax_error(c);
    }
}
