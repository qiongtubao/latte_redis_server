

#include "command_manager.h"
#include "zmalloc/zmalloc.h"
#include "dict/dict_plugins.h"
#include "../shared/shared.h"
#include "../redis/db.h"
#include "../redis/server.h"
#include "time/localtime.h"
#include "utils/utils.h"
#include "../object/string.h"
#include "../../deps/latte_c/src/odb/odb.h"
#include "../../deps/latte_c/src/object/object_manager.h"
#include "../../deps/latte_c/src/dict/dict.h"
#include "../../deps/latte_c/src/iterator/iterator.h"
#include "../../deps/latte_c/src/error/error.h"
#include <stdio.h>

/**
 * ACL命令分类映射表：将分类名称字符串映射到对应的标志位
 * 用于解析命令定义中的sflags字符串，支持基本标志和@开头的分类标志
 */
struct acl_category_item_t acl_command_categories[] = {
    /* 基本命令标志 */
    {"write", CMD_WRITE|CMD_CATEGORY_WRITE},              /* 写操作标志组合 */
    {"read-only", CMD_READONLY|CMD_CATEGORY_READ},         /* 只读操作标志组合 */
    {"use-memory", CMD_DENYOOM},                          /* 使用内存标志（OOM时拒绝） */
    {"admin", CMD_ADMIN|CMD_CATEGORY_ADMIN|CMD_CATEGORY_DANGEROUS}, /* 管理员+危险操作 */
    {"pub-sub", CMD_PUBSUB|CMD_CATEGORY_PUBSUB},          /* 发布订阅标志组合 */
    {"no-script", CMD_NOSCRIPT},                          /* 禁止脚本中执行 */
    {"random", CMD_RANDOM},                               /* 随机结果标志 */
    {"to-sort", CMD_SORT_FOR_SCRIPT},                     /* 脚本中需排序 */
    {"ok-loading", CMD_LOADING},                          /* 加载时允许执行 */
    {"ok-stale", CMD_STALE},                             /* 允许读取过期数据 */
    {"no-monitor", CMD_SKIP_MONITOR},                     /* 跳过监控记录 */
    {"no-slowlog", CMD_SKIP_SLOWLOG},                     /* 跳过慢日志记录 */
    {"cluster-asking", CMD_ASKING},                       /* 集群询问标志 */
    {"fast", CMD_FAST | CMD_CATEGORY_FAST},               /* 快速命令标志组合 */
    {"no-auth", CMD_NO_AUTH},                            /* 无需认证标志 */
    {"may-replicate", CMD_MAY_REPLICATE},                 /* 可能需要复制 */

    /* ACL分类标志（@开头表示权限类别） */
    {"@keyspace", CMD_CATEGORY_KEYSPACE},                 /* 键空间操作分类 */
    {"@read", CMD_CATEGORY_READ},                         /* 读操作分类 */
    {"@write", CMD_CATEGORY_WRITE},                       /* 写操作分类 */
    {"@set", CMD_CATEGORY_SET},                           /* 集合类型分类 */
    {"@sortedset", CMD_CATEGORY_SORTEDSET},               /* 有序集合类型分类 */
    {"@list", CMD_CATEGORY_LIST},                         /* 列表类型分类 */
    {"@hash", CMD_CATEGORY_HASH},                         /* 哈希类型分类 */
    {"@string", CMD_CATEGORY_STRING},                     /* 字符串类型分类 */
    {"@bitmap", CMD_CATEGORY_BITMAP},                     /* 位图类型分类 */
    {"@hyperloglog", CMD_CATEGORY_HYPERLOGLOG},           /* HyperLogLog类型分类 */
    {"@geo", CMD_CATEGORY_GEO},                          /* 地理位置类型分类 */
    {"@stream", CMD_CATEGORY_STREAM},                     /* 流类型分类 */
    {"@pubsub", CMD_CATEGORY_PUBSUB},                     /* 发布订阅分类 */
    {"@admin", CMD_CATEGORY_ADMIN},                       /* 管理员命令分类 */
    {"@fast", CMD_CATEGORY_FAST},                         /* 快速命令分类 */
    {"@slow", CMD_CATEGORY_SLOW},                         /* 慢速命令分类 */
    {"@blocking", CMD_CATEGORY_BLOCKING},                 /* 阻塞命令分类 */
    {"@dangerous", CMD_CATEGORY_DANGEROUS},               /* 危险命令分类 */
    {"@connection", CMD_CATEGORY_CONNECTION},             /* 连接管理分类 */
    {"@transaction", CMD_CATEGORY_TRANSACTION},           /* 事务分类 */
    {"@scripting", CMD_CATEGORY_SCRIPTING},               /* 脚本分类 */

    /* 内存交换相关分类标志 */
    {"@swap_keyspace", CMD_SWAP_DATATYPE_KEYSPACE},       /* 键空间交换分类 */
    {"@swap_string", CMD_SWAP_DATATYPE_STRING},           /* 字符串交换分类 */
    {"@swap_hash", CMD_SWAP_DATATYPE_HASH},               /* 哈希交换分类 */
    {"@swap_set", CMD_SWAP_DATATYPE_SET},                 /* 集合交换分类 */
    {"@swap_zset", CMD_SWAP_DATATYPE_ZSET},               /* 有序集合交换分类 */
    {"@swap_list", CMD_SWAP_DATATYPE_LIST},               /* 列表交换分类 */
    {"@swap_bitmap", CMD_SWAP_DATATYPE_BITMAP},           /* 位图交换分类 */
    {NULL,0} /* 数组结束标记 */
};


/**
 * 根据分类名称查找对应的命令标志位
 * 输入: name - 分类名称字符串（如"write"、"@admin"等）
 * 返回: 匹配的标志位，未找到返回0
 * 功能: 遍历acl_command_categories数组，进行大小写不敏感的字符串比较
 */
uint64_t command_data_type_flag_by_name(const char *name) {
    for (int j = 0; acl_command_categories[j].flag != 0; j++) {
        if (!strcasecmp(name,acl_command_categories[j].name)) {
            return acl_command_categories[j].flag;
        }
    }
    return 0; /* 未找到匹配项 */
}

/**
 * 解析标志字符串并设置到命令结构体中
 * 输入: c - 要设置标志的命令结构体指针, strflags - 空格分隔的标志字符串（如"write fast"）
 * 返回: 成功返回0，解析失败返回-1
 * 功能: 1. 用sds_split_args分割标志字符串为数组
 *       2. 逐个查找每个标志名对应的位掩码并设置到c->flags
 *       3. 若命令未标记为@fast则自动添加@slow标志（二元分类）
 */
int populate_command_table_parse_flags(struct redis_command_t *c, char *strflags) {
    int argc;
    sds *argv;
    int catflag;

    /* 将标志字符串分割为参数数组进行处理 */
    argv = sds_split_args(strflags,&argc);
    if (argv == NULL) return -1;

    /* 遍历每个标志名，查找对应的位掩码并设置到命令的flags字段 */
    for (int j = 0; j < argc; j++) {
        char *flag = argv[j];

        if((catflag = command_data_type_flag_by_name(flag)) != 0) {
            c->flags |= catflag;  /* 按位或操作设置标志位 */
        } else {
            /* 遇到无法识别的标志名时释放内存并返回错误 */
            sds_free_splitres(argv,argc);
            return -1;
        }
    }
    /* 在这个二元世界中：如果不是@fast命令，则默认为@slow命令 */
    if (!(c->flags & CMD_CATEGORY_FAST)) c->flags |= CMD_CATEGORY_SLOW;

    sds_free_splitres(argv,argc);
    return 0;
}

/**
 * 在命令管理器中查找指定名称的命令
 * 输入: cm - 命令管理器指针, command - 命令名称（sds字符串）
 * 返回: 找到的命令结构体指针，未找到返回NULL
 * 功能: 使用字典查找，支持大小写不敏感匹配
 */
struct redis_command_t* command_manager_lookup(command_manager_t* cm, sds command) {
    return dict_fetch_value(cm->commands, command);
}




#define USER_COMMAND_BITS_COUNT 1024  /* 用户命令位图容量：支持最多1024个命令的ACL权限控制 */

/**
 * 为ACL权限控制获取或分配命令ID
 * 输入: cm - 命令管理器指针, cmdname - 命令名称字符串
 * 返回: 命令的唯一ID（用于ACL位图索引）
 *
 * 功能: 每个用户都有一个位图记录允许执行的命令，每个命令需要唯一ID来索引位图。
 *       此函数使用顺序ID分配，对相同命令名重用相同ID，以支持模块的卸载和重新加载。
 *
 * 实现逻辑:
 *   1. 将命令名转为小写存储在rax基数树中
 *   2. 如果命令已存在，直接返回已分配的ID
 *   3. 如果是新命令，分配nextid作为新ID并递增nextid
 *   4. 特殊处理：永不分配最后一位(USER_COMMAND_BITS_COUNT-1)，该位用于标识
 *      ACL创建方式（+@all后减法 vs 从无权限开始加法），便于ACL SAVE时重写
 */
unsigned long acl_get_command_id(command_manager_t* cm, const char *cmdname) {

    /* 转换命令名为小写，确保大小写不敏感 */
    sds lowername = sds_new(cmdname);
    sds_to_lower(lowername);
    void *id;

    /* 在rax基数树中查找已存在的命令ID */
    if (raxFind(cm->commandId,(unsigned char*)lowername,sds_len(lowername),&id)) {
        sds_delete(lowername);
        return (unsigned long)id;
    }

    /* 为新命令分配ID并插入到rax树中 */
    raxInsert(cm->commandId,(unsigned char*)lowername,strlen(lowername),
              (void*)cm->nextid,NULL);
    sds_delete(lowername);
    unsigned long thisid = cm->nextid;
    cm->nextid++;

    /* 我们永远不分配用户命令位图结构中的最后一位，
     * 这样我们后续可以检查该位是否被设置，从而理解当前用户的ACL
     * 是从+@all开始创建（添加所有可能命令然后减去其他单个命令或分类），
     * 还是从头开始创建（只添加命令和分类，默认不允许未来的命令，如通过模块加载的）。
     * 这在使用ACL SAVE重写ACL时很有用。 */
    if (cm->nextid == USER_COMMAND_BITS_COUNT-1) cm->nextid++;
    return thisid;
}

/*  ==================== commands ==================== */
/* Command implementations are now in separate files:
 *   - expire.c: expire_command
 *   - ping.c: ping_command
 *   - quit.c: quit_command
 *   - save.c: save_command, load_command
 */

/* Forward declarations */
void expire_command(redis_client_t* c);
void ping_command(redis_client_t* c);
void quit_command(redis_client_t* c);
void save_command(redis_client_t* c);
void load_command(redis_client_t* c);
void slaveof_command(redis_client_t* c);
void sync_command(redis_client_t* c);

/*  ==================== command table ==================== */

/* ==================== 内置命令表 ==================== */
/**
 * 内置命令定义表：包含服务器启动时自动注册的核心命令
 * 每个命令包含：名称、处理函数、参数数量、标志字符串、其他属性
 *
 * 命令列表：
 * - quit: 关闭客户端连接
 * - module: 模块管理（支持-2表示可变参数：最少2个参数）
 * - ping: 连接测试（支持-2表示1-2个参数）
 * - info: 服务器信息查询（支持-2表示可变参数）
 * - expire: 设置键过期时间（固定3个参数）
 * - save: 数据持久化到文件（支持-1表示可变参数数量）
 * - load: 从文件恢复数据（支持-1表示可变参数数量）
 */
struct redis_command_t redis_command_table[] = {
    {
        "quit", quit_command, 0,        /* QUIT命令：无参数 */
        "admin",                        /* 管理员权限 */
        0, NULL, NULL, SWAP_NOP, 0, 0, 0, 0, 0, 0, 0
    },
    {
        "module", module_command, -2,   /* MODULE命令：最少2个参数（module + 子命令） */
        "admin no-script",              /* 管理员权限，禁止在脚本中执行 */
        0, NULL, NULL, SWAP_NOP, 0, 0, 0, 0, 0, 0, 0
    },
    {
        "ping", ping_command, -2,       /* PING命令：1-2个参数（ping 或 ping message） */
        "admin",                        /* 管理员权限 */
        0, NULL, NULL, SWAP_NOP, 0, 0, 0, 0, 0, 0, 0
    },
    {
        "info", info_command, -2,       /* INFO命令：1-多个参数（info 或 info section） */
        "admin",                        /* 管理员权限 */
        0, NULL, NULL, SWAP_NOP, 0, 0, 0, 0, 0, 0, 0
    },
    {
        "expire", expire_command, 3,    /* EXPIRE命令：固定3个参数（expire key seconds） */
        "write fast",                   /* 写操作，快速命令 */
        0, NULL, NULL, SWAP_NOP, 0, 0, 0, 0, 0, 0, 0
    },
    {
        "save", save_command, -1,       /* SAVE命令：可变参数（save 或 save filename） */
        "admin",                        /* 管理员权限 */
        0, NULL, NULL, SWAP_NOP, 0, 0, 0, 0, 0, 0, 0
    },
    {
        "load", load_command, -1,       /* LOAD命令：可变参数（load 或 load filename） */
        "admin",                        /* 管理员权限 */
        0, NULL, NULL, SWAP_NOP, 0, 0, 0, 0, 0, 0, 0
    },
    {
        "slaveof", slaveof_command, 3,  /* SLAVEOF命令：3个参数（slaveof host port 或 slaveof no one） */
        "admin",                        /* 管理员权限 */
        0, NULL, NULL, SWAP_NOP, 0, 0, 0, 0, 0, 0, 0
    },
    {
        "sync", sync_command, 1,        /* SYNC命令：1个参数（slave握手时发送，master执行全量同步） */
        "admin no-monitor",             /* 管理员权限，不记录到 MONITOR */
        0, NULL, NULL, SWAP_NOP, 0, 0, 0, 0, 0, 0, 0
    }
};

/**
 * 注册单个命令到命令管理器
 * 输入: cm - 命令管理器指针, cmd - 要注册的命令结构体指针
 * 返回: 成功返回0，失败返回-1
 * 功能: 1. 为命令分配唯一ID（用于ACL权限控制）
 *       2. 将命令添加到字典中，键为命令名，值为命令结构体指针
 */
int command_manager_register_command(command_manager_t* cm, redis_command_t* cmd) {
    cmd->id = acl_get_command_id(cm, cmd->name);  /* 获取ACL权限位图中的唯一索引 */
    return dict_add(cm->commands, sds_new(cmd->name), cmd);
}

/**
 * 初始化命令管理器：注册所有内置命令
 * 输入: cm - 已创建的命令管理器指针
 * 功能: 1. 遍历redis_command_table数组中的所有内置命令
 *       2. 解析每个命令的sflags字符串为flags位掩码
 *       3. 调用command_manager_register_command注册命令
 *       4. 记录调试日志
 */
void command_manager_init(command_manager_t* cm) {
    int j;
    int num_commands = sizeof(redis_command_table)/sizeof(struct redis_command_t);

    /* 遍历内置命令表，逐个注册命令 */
    for (j = 0; j < num_commands; j++) {
        struct redis_command_t *c = redis_command_table + j;
        int retval1, retval2;

        /* 解析字符串标志为位掩码：将sflags（如"write fast"）转换为flags位字段 */
        if (populate_command_table_parse_flags(c,  c->sflags) == -1) {
            latte_panic("unsupported command flag");  /* 遇到不支持的标志时终止程序 */
        }

        /* 注册命令到管理器中 */
        retval1 = command_manager_register_command(cm, c);
        LATTE_LIB_LOG(LOG_DEBUG, "register %s command", c->name);
    }
}



/**
 * 命令字典类型定义：支持大小写不敏感的命令名查找
 * 使用sds字符串作为键，支持case-insensitive比较和哈希
 * 配置了键的析构函数，值不需要析构（指向静态命令表）
 */
dict_func_t command_table_dict_type = {
    dict_sds_case_hash,        /* 大小写不敏感的sds哈希函数 */
    NULL,                      /* 无键复制函数 */
    NULL,                      /* 无值复制函数 */
    dict_sds_key_case_compare, /* 大小写不敏感的sds键比较函数 */
    dict_sds_destructor,       /* sds键的析构函数 */
    NULL,                      /* 无值析构函数（命令结构体是静态的） */
    NULL                       /* 无扩展哈希函数 */
};

/**
 * 创建新的命令管理器实例
 * 返回: 初始化完成的命令管理器指针，失败返回NULL
 * 功能: 1. 分配命令管理器结构体内存
 *       2. 创建命令字典（支持大小写不敏感查找）
 *       3. 创建命令ID基数树（用于ACL权限管理）
 *       4. 初始化nextid为0
 *       5. 调用command_manager_init注册所有内置命令
 */
command_manager_t* command_manager_new() {
    command_manager_t* cm = (command_manager_t*)zmalloc(sizeof(command_manager_t));
    cm->commands = dict_new(&command_table_dict_type);    /* 创建大小写不敏感的命令字典 */
    cm->commandId = raxNew();                             /* 创建基数树存储命令ID映射 */
    cm->nextid = 0;                                       /* 初始化命令ID计数器 */
    command_manager_init(cm);                             /* 注册内置命令表 */
    return cm;
}

/**
 * 删除命令管理器实例，释放所有相关资源
 * 输入: cm - 要删除的命令管理器指针
 * 功能: 1. 删除命令字典（自动释放所有键值对）
 *       2. 释放命令ID基数树
 *       3. 释放管理器结构体内存
 */
void command_manager_delete(command_manager_t* cm) {
    dict_delete(cm->commands);    /* 删除字典会自动调用键的析构函数释放sds */
    raxFree(cm->commandId);       /* 释放基数树 */
    zfree(cm);                    /* 释放管理器结构体 */
}



