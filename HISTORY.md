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
