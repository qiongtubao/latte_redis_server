/**
 * replication.h - 主从复制模块头文件
 *
 * 定义主从复制相关的内部结构和函数声明。
 * 对外接口已在 server.h 中声明，本文件供 replication.c 和 slaveof.c 内部使用。
 */
#ifndef __REDIS_REPLICATION_H
#define __REDIS_REPLICATION_H

#include "server.h"
#include "client.h"

/* ========== 复制协议魔数 ========== */
#define REPL_PROTO_SYNC_CMD    "SYNC\r\n"          /* 全量同步请求命令 */
#define REPL_PROTO_PING_CMD    "PING\r\n"          /* 心跳探测命令 */
#define REPL_PROTO_EOF_MARK    "$-1\r\n"           /* 全量数据传输完毕标记（bulk null） */

/* ========== 复制传输缓冲区大小 ========== */
#define REPL_SYNCIO_TIMEOUT    5000                /* 同步 IO 超时时间（毫秒） */
#define REPL_READ_BUF_SIZE     (64 * 1024)         /* 增量接收缓冲区大小（64KB） */

/* ========== 内部辅助函数 ========== */

/**
 * 输入: server - 服务器实例
 * 输出/返回: 无
 * 功能: slave 侧：连接建立后的读事件处理，驱动握手状态机
 *       - REPL_STATE_WAIT_PONG: 等待 PONG 回复，收到后进入全量同步
 *       - REPL_STATE_TRANSFER:  接收全量数据并写入 repl_transfer_buf
 *       - REPL_STATE_CONNECTED: 接收增量命令并执行
 */
void replication_read_handler(connection* conn);

/**
 * 输入: conn - 与 master 建立的连接
 * 输出/返回: 无
 * 功能: slave 侧：TCP 连接完成的回调，发送 PING 进入握手流程
 */
void replication_connect_handler(connection* conn);

/**
 * 输入: server - 服务器实例
 * 输出/返回: 0 成功，-1 失败
 * 功能: slave 侧：全量数据接收完毕后，调用 load 流程恢复数据到本地 DB
 */
int replication_load_transfer_buf(redis_server_t* server);

#endif /* __REDIS_REPLICATION_H */
