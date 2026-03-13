#include "command_manager.h"
#include "../shared/shared.h"

/**
 * QUIT命令实现：优雅地关闭客户端连接
 * 输入: c - 客户端连接对象
 * 用法: QUIT
 * 功能: 1. 发送"OK"确认回复给客户端
 *       2. 设置连接关闭标志，让服务器在回复发送后关闭连接
 * 这种设计确保客户端能收到确认回复，然后连接被优雅关闭
 */
void quit_command(redis_client_t* c) {
    add_reply(c, shared.ok);                          /* 发送OK回复确认命令已处理 */
    c->client.flags |= CLIENT_CLOSE_AFTER_REPLY;      /* 设置标志：回复发送后关闭连接 */
}
