# EXPIRE 命令单测：设置 key 过期，访问时惰性删除

test "EXPIRE on existing key returns 1" {
    set srv [start_latte_redis_server_with_modules]
    after 100
    update
    set r [redis_cli_from_srv $srv]

    $r set x 1
    set reply [$r expire x 10]
    assert {$reply eq "1"}

    set v [$r get x]
    assert {$v eq "1"}

    $r close
    kill_latte_server $srv
}

test "EXPIRE on non-existing key returns 0" {
    set srv [start_latte_redis_server_with_modules]
    after 100
    update
    set r [redis_cli_from_srv $srv]

    set reply [$r expire nonexistent 10]
    assert {$reply eq "0"}

    $r close
    kill_latte_server $srv
}

test "Expired key is not returned by GET (lazy expire)" {
    set srv [start_latte_redis_server_with_modules]
    after 100
    update
    set r [redis_cli_from_srv $srv]

    $r set ttlkey ttlvalue
    $r expire ttlkey 1
    after 1500
    update

    set v [$r get ttlkey]
    assert {$v eq {}}

    $r close
    kill_latte_server $srv
}
