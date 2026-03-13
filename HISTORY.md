# 项目变更历史

本文档记录 latte_redis_server 项目的变更历史，包括新功能、Bug 修复和其他重要更新。

## 格式说明

每个条目应遵循以下格式：
```
### YYYY-MM-DD - [类型] 简要描述
- **文件**: 受影响的文件列表
- **详情**: 变更的详细说明
- **相关**: 相关的问题、PR 或功能
```

类型说明：
- `Feature`: 新功能
- `Bugfix`: Bug 修复
- `Refactor`: 代码重构
- `Performance`: 性能优化
- `Documentation`: 文档更新
- `Test`: 测试相关

---

## 2026-03-03 - [Feature] 添加 SAVE 和 LOAD 命令

- **文件**: 
  - `src/commands/command_manager.c`
  - `deps/latte_c/src/rdb/rdb.c`
  - `deps/latte_c/src/rdb/rdb.h`
  - `src/redis/db.h`
  - `src/redis/db.c`
- **详情**: 
  - 实现了 `SAVE` 命令，将数据库状态保存到 LDB 文件
  - 实现了 `LOAD` 命令，从 LDB 文件加载数据库状态
  - 添加了对保存/加载过期时间的支持
  - 添加了处理毫秒时间戳的 RDB 函数（`rdb_save_milliseconds`, `rdb_load_milliseconds`）
  - SAVE 命令会阻塞服务器并保存所有键、值和过期时间
  - LOAD 命令会清空现有数据并从 LDB 文件加载所有数据
- **相关**: 持久化功能的初始实现

## 2026-03-03 - [Bugfix] 修复 Linux 兼容性编译问题

- **文件**: 
  - `src/modules/Makefile`
- **详情**: 
  - 为 Linux 和 macOS 平台在 `SHOBJ_CFLAGS` 中添加了 `-fPIC` 标志
  - 确保 `.c.xo` 规则正确使用 `SHOBJ_CFLAGS`
  - 修复了在 Linux 上构建共享对象时的重定位错误
- **相关**: 跨平台兼容性

## 2026-03-03 - [Bugfix] 修复 RDB_LENERR 定义

- **文件**: 
  - `deps/latte_c/src/rdb/rdb.h`
- **详情**: 
  - 在头文件中添加了 `RDB_LENERR` 定义（`UINT64_MAX`）
  - 修复了 `RDB_LENERR` 未定义的编译错误
- **相关**: RDB 实现

## 2026-03-03 - [Bugfix] 改进 LOAD 命令的错误处理和数据清空逻辑

- **文件**: 
  - `src/commands/command_manager.c`
- **详情**: 
  - 改进了 `load_command` 中文件 I/O 操作的错误处理
  - 修复了数据清空逻辑，使用两遍遍历方式（先收集键，再删除）
  - 添加了对数据库、字典和迭代器对象的空值检查
  - 修复了过期时间设置，使用数据库中实际存储的键
  - 改进了键删除的内存管理
- **相关**: LOAD 命令稳定性

## 2026-03-03 - [Feature] 添加 kv_store_get_dict 函数

- **文件**: 
  - `src/redis/db.h`
  - `src/redis/db.c`
- **详情**: 
  - 添加了 `kv_store_get_dict` 函数的声明和实现
  - 允许访问 kv_store 中的单个字典
- **相关**: SAVE/LOAD 实现

## 2026-03-04 - [Refactor] 命令模块化分离

- **文件**: 
  - `src/commands/command_manager.c`
  - `src/commands/command_manager.h`
  - `src/commands/expire.c` (新建)
  - `src/commands/ping.c` (新建)
  - `src/commands/quit.c` (新建)
  - `src/commands/save.c` (新建)
  - `src/Makefile`
- **详情**: 
  - 将命令实现从 `command_manager.c` 中分离到独立文件，实现模块化
  - 创建的新文件：
    - `expire.c` - 包含 `expire_command` 实现
    - `ping.c` - 包含 `ping_command` 实现
    - `quit.c` - 包含 `quit_command` 实现
    - `save.c` - 包含 `save_command` 和 `load_command` 实现
  - `command_manager.c` 现在只包含：
    - 命令管理逻辑（注册、查找等）
    - 命令表定义
    - 前向声明
  - 移除了 `command_manager.c` 中不必要的 include（odb、object_manager 等）
  - 更新了 `command_manager.h`，添加了新命令的声明
  - 更新了 `Makefile`，将新的命令文件添加到编译列表
  - 优势：
    - 提高代码可维护性，每个命令独立文件
    - 便于扩展，新增命令只需添加新文件
    - 代码组织更清晰，命令管理逻辑与实现分离
- **相关**: 代码重构和模块化

## 2026-03-03 - [Test] 添加 SAVE 和 LOAD 命令测试

- **文件**: 
  - `tests/commands/save.tcl`
  - `tests/test_main.tcl`
- **详情**: 
  - 为 SAVE 命令添加了完整的测试套件
  - 为 LOAD 命令添加了完整的测试套件
  - 测试覆盖以下场景：
    - 保存数据到默认的 dump.ldb 文件
    - 保存数据到自定义文件名
    - 从 dump.ldb 文件加载数据
    - 从自定义文件名加载数据
    - 保存/加载过程中过期时间的保留
    - 加载操作前的数据清空
  - 在 `test_main.tcl` 中将 `commands/save` 添加到测试套件
- **相关**: SAVE/LOAD 功能实现

## 2026-03-04 - [Test] 改进测试服务器启动功能，添加日志和输出文件管理

- **文件**: 
  - `tests/support/server.tcl`
- **详情**: 
  - 为每个测试服务器实例创建独立的工作目录
    - 目录格式：`tests/tmp/server_[pid]_[timestamp]`
    - 每个服务器实例拥有独立的输出和日志目录
  - 重定向服务器输出到文件
    - `stdout.log` - 标准输出
    - `stderr.log` - 标准错误输出
    - 使用 Tcl 的 `exec` 重定向语法实现
  - 配置服务器日志文件
    - `server.log` - 服务器日志文件
    - 通过 `--log-file` 参数传递给服务器
    - 设置日志级别为 `debug`，便于调试
  - 修改的函数：
    - `start_latte_redis_server` - 基础服务器启动函数
    - `start_latte_redis_server_with_modules` - 带模块的服务器启动函数
    - `start_latte_redis_server_with_modules_no_ping` - 无 ping 的服务器启动函数
  - 返回的 dict 新增字段：
    - `stdout_file` - stdout 文件路径
    - `stderr_file` - stderr 文件路径
    - `log_file` - 日志文件路径
    - `workdir` - 工作目录路径
  - 保留日志文件供调试
    - `kill_latte_server` 不再删除工作目录，保留所有日志文件
- **相关**: 测试调试和问题排查

## 2026-03-04 - [Refactor] 合并测试服务器启动函数

- **文件**: 
  - `tests/support/server.tcl`
- **详情**: 
  - 将三个独立的服务器启动函数合并为一个统一的 `start_latte_redis_server` 函数
  - 统一的函数支持以下参数：
    - `overrides` - 端口等覆盖参数
    - `module_paths` - 模块路径列表，空列表表示不加载模块
    - `use_ping` - 是否使用 ping 检测服务器就绪，默认 true
    - `wait_after_ping` - ping 检测后等待时间（毫秒），默认 400
    - `wait_ms` - 不使用 ping 时直接等待的时间（毫秒），默认 1500
  - 保留旧函数作为兼容性包装器：
    - `start_latte_redis_server_with_modules` - 调用统一函数，传递模块路径，使用 ping 检测
    - `start_latte_redis_server_with_modules_no_ping` - 调用统一函数，传递模块路径，不使用 ping
  - 优势：
    - 减少代码重复，从 271 行减少到 220 行
    - 统一管理服务器启动逻辑
    - 保持向后兼容，所有现有测试无需修改
- **相关**: 测试基础设施改进

---

## 新条目模板

```
### YYYY-MM-DD - [类型] 简要描述

- **文件**: 
  - `路径/到/文件1.c`
  - `路径/到/文件2.h`
- **详情**: 
  - 详细描述变更内容
  - 说明为什么进行此变更
  - 说明如何工作
- **相关**: 相关的问题、功能或上下文
```
