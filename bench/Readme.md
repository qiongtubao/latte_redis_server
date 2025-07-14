## 基础用法:
    ./bench {host}:{port} {password} {commnd-args} {maxkey} {preix} {thread} {limit}

## 支持命令:
### ping
    ./bench 127.0.0.1:6666 nopass ping {max-key} {preix} {thread} {limit}
### set
    ./bench 127.0.0.1:6666 nopass set-{value size} {max-key} {preix} {thread} {limit}
### get
    ./bench 127.0.0.1:6666 nopass get {max-key}  {preix}  {thread} {limit}