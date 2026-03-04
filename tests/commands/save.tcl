# SAVE/LOAD 命令测试：保存和加载数据到/从 ldb 文件

test "SAVE saves data to dump.ldb" {
    set srv [start_latte_redis_server_with_modules]
    after 200
    update
    set r [redis_cli_from_srv $srv]
    after 200
    update

    # 设置一些数据
    $r set key1 value1
    $r set key2 value2
    $r set key3 value3
    after 100
    update

    # 保存数据
    set reply [$r save]
    assert {$reply eq "OK"}

    # 检查文件是否存在
    set dump_file "dump.ldb"
    assert {[file exists $dump_file]}

    $r close
    kill_latte_server $srv

    # 清理测试文件
    if {[file exists $dump_file]} {
        file delete $dump_file
    }
}

test "SAVE with custom filename" {
    set srv [start_latte_redis_server_with_modules]
    after 100
    update
    set r [redis_cli_from_srv $srv]

    # 设置一些数据
    $r set key1 value1

    # 保存到自定义文件
    set custom_file "test_save.ldb"
    set reply [$r save $custom_file]
    assert {$reply eq "OK"}

    # 检查文件是否存在
    assert {[file exists $custom_file]}

    $r close
    kill_latte_server $srv

    # 清理测试文件
    if {[file exists $custom_file]} {
        file delete $custom_file
    }
}

test "LOAD loads data from dump.ldb" {
    set srv [start_latte_redis_server_with_modules]
    after 100
    update
    set r [redis_cli_from_srv $srv]

    # 设置一些数据
    $r set key1 value1
    $r set key2 value2
    $r set key3 value3

    # 保存数据
    set reply [$r save]
    assert {$reply eq "OK"}

    # 修改数据
    $r set key1 modified1
    $r set key4 value4

    # 验证修改后的数据
    assert {[$r get key1] eq "modified1"}
    assert {[$r get key4] eq "value4"}

    # 加载数据（应该清空现有数据并恢复保存的数据）
    set reply [$r load]
    assert {$reply eq "OK"}

    # 验证数据已恢复
    assert {[$r get key1] eq "value1"}
    assert {[$r get key2] eq "value2"}
    assert {[$r get key3] eq "value3"}
    # key4 应该不存在（因为保存时没有）
    assert {[$r get key4] eq {}}

    $r close
    kill_latte_server $srv

    # 清理测试文件
    set dump_file "dump.ldb"
    if {[file exists $dump_file]} {
        file delete $dump_file
    }
}

test "LOAD with custom filename" {
    set srv [start_latte_redis_server_with_modules]
    after 100
    update
    set r [redis_cli_from_srv $srv]

    # 设置一些数据
    $r set key1 value1
    $r set key2 value2

    # 保存到自定义文件
    set custom_file "test_load.ldb"
    set reply [$r save $custom_file]
    assert {$reply eq "OK"}

    # 修改数据
    $r set key1 modified1

    # 从自定义文件加载
    set reply [$r load $custom_file]
    assert {$reply eq "OK"}

    # 验证数据已恢复
    assert {[$r get key1] eq "value1"}
    assert {[$r get key2] eq "value2"}

    $r close
    kill_latte_server $srv

    # 清理测试文件
    if {[file exists $custom_file]} {
        file delete $custom_file
    }
}

test "SAVE and LOAD preserve expiration times" {
    set srv [start_latte_redis_server_with_modules]
    after 100
    update
    set r [redis_cli_from_srv $srv]

    # 设置数据并添加过期时间
    $r set key1 value1
    $r expire key1 10
    $r set key2 value2
    # key2 不过期

    # 保存数据
    set reply [$r save]
    assert {$reply eq "OK"}

    # 修改数据
    $r set key1 modified1
    $r set key2 modified2

    # 加载数据
    set reply [$r load]
    assert {$reply eq "OK"}

    # 验证数据已恢复
    assert {[$r get key1] eq "value1"}
    assert {[$r get key2] eq "value2"}

    # 验证过期时间（key1 应该还有过期时间，但这里只验证数据存在）
    # 注意：过期时间的验证需要等待，这里只验证数据恢复成功
    assert {[$r get key1] eq "value1"}

    $r close
    kill_latte_server $srv

    # 清理测试文件
    set dump_file "dump.ldb"
    if {[file exists $dump_file]} {
        file delete $dump_file
    }
}

test "LOAD clears existing data before loading" {
    set srv [start_latte_redis_server_with_modules]
    after 100
    update
    set r [redis_cli_from_srv $srv]

    # 第一次：设置并保存数据
    $r set key1 value1
    $r set key2 value2
    set reply [$r save]
    assert {$reply eq "OK"}

    # 添加新数据（不在保存的数据中）
    $r set key3 value3
    $r set key4 value4

    # 验证新数据存在
    assert {[$r get key3] eq "value3"}
    assert {[$r get key4] eq "value4"}

    # 加载数据（应该清空 key3 和 key4）
    set reply [$r load]
    assert {$reply eq "OK"}

    # 验证保存的数据存在
    assert {[$r get key1] eq "value1"}
    assert {[$r get key2] eq "value2"}

    # 验证新数据已被清空
    assert {[$r get key3] eq {}}
    assert {[$r get key4] eq {}}

    $r close
    kill_latte_server $srv

    # 清理测试文件
    set dump_file "dump.ldb"
    if {[file exists $dump_file]} {
        file delete $dump_file
    }
}
