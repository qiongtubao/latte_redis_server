#include "command_manager.h"
#include "../shared/shared.h"

/**
 * PING命令实现：测试客户端与服务器之间的连接
 * 输入: c - 客户端连接对象
 * 用法: PING [message]
 * 功能: 用于检测连接是否正常，支持两种模式：
 *       - 无参数：返回简单字符串"PONG"
 *       - 带参数：返回该参数的bulk字符串回复
 * 返回: PONG（无参数）或参数的原始内容（有参数）
 */
void ping_command(redis_client_t* c) {
    /* 参数校验：最多支持2个参数（PING 或 PING message） */
    if (c->argc > 2) {
        add_reply_error_format(c, "wrong number of arguments for '%s' command",
            c->cmd->name);
        return;
    }

    if (c->argc == 1) {
        /* PING无参数模式：返回预定义的共享"PONG"字符串 */
        add_reply(c, shared.pong);
    } else {
        /* PING带消息模式：将argv[1]作为bulk字符串返回给客户端
         * 这样客户端发送什么消息，服务器就原样返回什么消息 */
        add_reply_bulk(c, c->argv[1]);
    }
}
