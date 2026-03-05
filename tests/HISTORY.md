# 测试变更历史

本文档记录测试相关的变更历史，包括新的测试用例、测试修复和测试基础设施改进。

## 格式说明

每个条目应遵循以下格式：
```
### YYYY-MM-DD - [类型] 简要描述
- **文件**: 受影响的测试文件列表
- **详情**: 测试变更的详细说明
- **相关**: 相关的功能或问题
```

类型说明：
- `Test`: 新测试用例
- `Fix`: 测试修复
- `Infrastructure`: 测试基础设施改进
- `Coverage`: 测试覆盖率改进

---

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

## 2026-03-03 - [Test] SAVE/LOAD 命令的测试基础设施

- **文件**: 
  - `tests/commands/save.tcl`
- **详情**: 
  - 创建了包含多个测试用例的 `tests/commands/save.tcl` 文件
  - 测试验证以下内容：
    - SAVE 后文件创建
    - SAVE/LOAD 循环中的数据持久化
    - 过期时间保留
    - 数据清空行为
    - 自定义文件名支持
- **相关**: 持久化功能测试

## 2026-03-04 - [Infrastructure] 改进测试服务器启动功能，添加日志和输出文件管理

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

---

## 新条目模板

```
### YYYY-MM-DD - [类型] 简要描述

- **文件**: 
  - `tests/路径/到/测试文件.tcl`
  - `tests/support/辅助文件.tcl`
- **详情**: 
  - 测试的内容
  - 覆盖的测试场景
  - 预期行为
- **相关**: 相关的功能或问题
```
