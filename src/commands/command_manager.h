#ifndef __COMMAND_MANAGER_H
#define __COMMAND_MANAGER_H


#include "../redis/command.h"
#include <string.h>
#include "debug/latte_debug.h"
#include "rax/rax.h"
#include "../redis/client.h"

/* OOM Score Adjustment classes. OOM优先级调整分类 */
#define CONFIG_OOM_MASTER 0   /* 主节点：最高优先级，最后被杀 */
#define CONFIG_OOM_REPLICA 1  /* 从节点：中等优先级 */
#define CONFIG_OOM_BGCHILD 2  /* 后台子进程：最低优先级，最先被杀 */
#define CONFIG_OOM_COUNT 3    /* OOM分类总数 */

extern int configOOMScoreAdjValuesDefaults[CONFIG_OOM_COUNT];

/* Hash table parameters 哈希表参数配置 */
#define HASHTABLE_MIN_FILL        10      /* 哈希表最小填充率10% */
#define HASHTABLE_MAX_LOAD_FACTOR 1.618   /* 哈希表最大负载因子（黄金比例） */

/* Command flags. Please check the command table defined in the server.c file
 * for more information about the meaning of every flag.
 * 命令标志位定义。每个位表示命令的不同特性和权限 */
#define CMD_WRITE (1ULL<<0)            /* 写操作标志：命令会修改数据 */
#define CMD_READONLY (1ULL<<1)         /* 只读标志：命令只读取数据，不修改 */
#define CMD_DENYOOM (1ULL<<2)          /* 内存使用标志：内存不足时拒绝执行 */
#define CMD_MODULE (1ULL<<3)           /* 模块命令标志：由外部模块导出的命令 */
#define CMD_ADMIN (1ULL<<4)            /* 管理员标志：需要管理员权限 */
#define CMD_PUBSUB (1ULL<<5)           /* 发布订阅标志：与pub/sub相关 */
#define CMD_NOSCRIPT (1ULL<<6)         /* 禁止脚本标志：不能在脚本中执行 */
#define CMD_RANDOM (1ULL<<7)           /* 随机标志：结果不确定，影响复制 */
#define CMD_SORT_FOR_SCRIPT (1ULL<<8)  /* 脚本排序标志：需要对键排序以保证一致性 */
#define CMD_LOADING (1ULL<<9)          /* 加载时允许标志：服务器加载时可执行 */
#define CMD_STALE (1ULL<<10)           /* 过期数据允许标志：可读取过期副本 */
#define CMD_SKIP_MONITOR (1ULL<<11)    /* 跳过监控标志：不记录到monitor */
#define CMD_SKIP_SLOWLOG (1ULL<<12)    /* 跳过慢日志标志：不记录到慢日志 */
#define CMD_ASKING (1ULL<<13)          /* 集群询问标志：集群重定向相关 */
#define CMD_FAST (1ULL<<14)            /* 快速标志：执行时间短的命令 */
#define CMD_NO_AUTH (1ULL<<15)         /* 无需认证标志：未认证时也可执行 */
#define CMD_MAY_REPLICATE (1ULL<<16)   /* 可能复制标志：可能产生写操作需复制 */

/* Command flags used by the module system. 模块系统专用标志 */
#define CMD_MODULE_GETKEYS (1ULL<<17)  /* 使用模块getkeys接口获取键名 */
#define CMD_MODULE_NO_CLUSTER (1ULL<<18) /* 在Redis集群中拒绝执行 */

/* Command flags that describe ACLs categories. ACL权限分类标志 */
#define CMD_CATEGORY_KEYSPACE (1ULL<<19)     /* 键空间操作分类 */
#define CMD_CATEGORY_READ (1ULL<<20)         /* 读操作分类 */
#define CMD_CATEGORY_WRITE (1ULL<<21)        /* 写操作分类 */
#define CMD_CATEGORY_SET (1ULL<<22)          /* 集合数据类型分类 */
#define CMD_CATEGORY_SORTEDSET (1ULL<<23)    /* 有序集合数据类型分类 */
#define CMD_CATEGORY_LIST (1ULL<<24)         /* 列表数据类型分类 */
#define CMD_CATEGORY_HASH (1ULL<<25)         /* 哈希数据类型分类 */
#define CMD_CATEGORY_STRING (1ULL<<26)       /* 字符串数据类型分类 */
#define CMD_CATEGORY_BITMAP (1ULL<<27)       /* 位图数据类型分类 */
#define CMD_CATEGORY_HYPERLOGLOG (1ULL<<28)  /* HyperLogLog数据类型分类 */
#define CMD_CATEGORY_GEO (1ULL<<29)          /* 地理位置数据类型分类 */
#define CMD_CATEGORY_STREAM (1ULL<<30)       /* 流数据类型分类 */
#define CMD_CATEGORY_PUBSUB (1ULL<<31)       /* 发布订阅分类 */
#define CMD_CATEGORY_ADMIN (1ULL<<32)        /* 管理员命令分类 */
#define CMD_CATEGORY_FAST (1ULL<<33)         /* 快速命令分类 */
#define CMD_CATEGORY_SLOW (1ULL<<34)         /* 慢速命令分类 */
#define CMD_CATEGORY_BLOCKING (1ULL<<35)     /* 阻塞命令分类 */
#define CMD_CATEGORY_DANGEROUS (1ULL<<36)    /* 危险命令分类 */
#define CMD_CATEGORY_CONNECTION (1ULL<<37)   /* 连接管理分类 */
#define CMD_CATEGORY_TRANSACTION (1ULL<<38)  /* 事务分类 */
#define CMD_CATEGORY_SCRIPTING (1ULL<<39)    /* 脚本分类 */

/* swap datatype flags 内存交换数据类型标志（用于内存优化） */
#define CMD_SWAP_DATATYPE_KEYSPACE (1ULL<<40)  /* 键空间交换标志 */
#define CMD_SWAP_DATATYPE_STRING (1ULL<<41)    /* 字符串类型交换标志 */
#define CMD_SWAP_DATATYPE_HASH (1ULL<<42)      /* 哈希类型交换标志 */
#define CMD_SWAP_DATATYPE_SET (1ULL<<43)       /* 集合类型交换标志 */
#define CMD_SWAP_DATATYPE_ZSET (1ULL<<44)      /* 有序集合类型交换标志 */
#define CMD_SWAP_DATATYPE_LIST (1ULL<<45)      /* 列表类型交换标志 */
#define CMD_SWAP_DATATYPE_BITMAP (1ULL<<46)    /* 位图类型交换标志 */

/* SWAP操作类型常量定义 */
#define SWAP_UNSET -1  /* 未设置交换状态 */
#define SWAP_NOP    0  /* 无操作 */
#define SWAP_IN     1  /* 从磁盘加载到内存 */
#define SWAP_OUT    2  /* 从内存写入磁盘 */
#define SWAP_DEL    3  /* 删除操作 */
#define SWAP_UTILS  4  /* 工具操作 */
#define SWAP_TYPES  5  /* 类型操作 */

/**
 * ACL分类项结构体：存储权限分类名称与对应标志位的映射关系
 */
typedef struct  acl_category_item_t {
    const char *name;  /* 分类名称，如"@string"、"@admin"等 */
    uint64_t flag;     /* 对应的标志位掩码 */
} acl_category_item_t;

/**
 * 命令管理器结构体：负责管理所有Redis命令的注册、查找和ID分配
 */
typedef struct command_manager_t {
    dict_t* commands;        /* 命令字典：命令名 -> redis_command_t* 映射，支持大小写不敏感查找 */
    rax *commandId;         /* 命令ID基数树：命令名(小写) -> ID 映射，用于ACL位图索引 */
    unsigned long nextid;   /* 下一个可分配的命令ID，用于新命令的唯一标识 */
} command_manager_t;

/**
 * 创建命令管理器实例
 * 返回: 新创建的命令管理器指针，失败返回NULL
 */
command_manager_t* command_manager_new();

/**
 * 删除命令管理器实例，释放所有相关资源
 * 输入: cm - 要删除的命令管理器指针
 */
void command_manager_delete(command_manager_t* cm);

/**
 * 根据命令名查找命令结构体（大小写不敏感）
 * 输入: cm - 命令管理器指针, command - 命令名字符串
 * 返回: 找到的命令结构体指针，未找到返回NULL
 */
redis_command_t* command_manager_lookup(command_manager_t* cm, sds command);

/**
 * 注册单个命令到命令管理器
 * 输入: cm - 命令管理器指针, cmd - 要注册的命令结构体指针
 * 返回: 成功返回0，失败返回-1
 */
int command_manager_register_command(command_manager_t* cm, redis_command_t* cmd);

/* ==================== 内置命令实现函数 ==================== */

/**
 * PING命令：测试连接
 * 输入: c - 客户端连接对象
 * 功能: 无参数返回"PONG"，有参数返回参数的bulk回复
 */
void ping_command(redis_client_t* c);

/**
 * QUIT命令：关闭连接
 * 输入: c - 客户端连接对象
 * 功能: 发送OK回复后设置连接关闭标志
 */
void quit_command(redis_client_t* c);

/**
 * MODULE命令：模块管理（HELP/LOAD/UNLOAD/LIST子命令）
 * 输入: c - 客户端连接对象
 * 功能: 根据子命令分发到相应的模块管理函数
 */
void module_command(redis_client_t* c);

/**
 * INFO命令：查看服务器信息
 * 输入: c - 客户端连接对象
 * 功能: 返回服务器运行状态信息（当前为占位实现）
 */
void info_command(redis_client_t* c);

/**
 * EXPIRE命令：设置键过期时间
 * 输入: c - 客户端连接对象
 * 功能: 解析key和seconds参数，设置毫秒级过期时间戳，返回1(成功)或0(键不存在)
 */
void expire_command(redis_client_t* c);

/**
 * SAVE命令：持久化数据到LDB文件
 * 输入: c - 客户端连接对象
 * 功能: 将当前所有数据库的键值对序列化保存到指定文件
 */
void save_command(redis_client_t* c);

/**
 * LOAD命令：从LDB文件恢复数据
 * 输入: c - 客户端连接对象
 * 功能: 清空当前数据库，从指定文件反序列化恢复所有键值对
 */
void load_command(redis_client_t* c);

#endif