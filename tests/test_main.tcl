# Redis test suite. Copyright (C) 2009 Salvatore Sanfilippo antirez@gmail.com
# This software is released under the BSD License. See the COPYING file for
# more information.

package require Tcl 8.5


set tcl_precision 17
source tests/support/util.tcl
source tests/support/test.tcl
source tests/support/instances.tcl

set ::all_tests {
    unit/version
}


#global
set ::host 127.0.0.1
# redis从哪个端口开始
set ::baseport 21111; # initial port for spawned redis servers
#单线程使用的port总数
set ::portcount 8000; # we don't wanna use more than 10000 to avoid collision with cluster bus ports
# 是否是client
set ::client 0
# 并发次数
set ::numclients 16
# 执行前是否清理
set ::dont_pre_clean 0
# 是否要清理
set ::dont_clean 0
# 0表示输出日志
set ::quiet 0
# 外部的
set ::external 0; # If "1" this means, we are running against external instance
# Index to the next test to run in the ::all_tests list.
# 运行到第几个测试的索引
set ::next_test 0

#特殊测试模式
set ::run_solo_tests {}

# 是否循环测试
set ::loop 0
# active servers pid
set ::active_servers {} ; # Pids of active Redis instances.

proc print_help_screen {} {
    puts [join {
        "--valgrind         Run the test over valgrind."
        "--durable          suppress test crashes and keep running"
        "--stack-logging    Enable OSX leaks/malloc stack logging."
        "--accurate         Run slow randomized tests for more iterations."
        "--quiet            Don't show individual tests."
        "--single <unit>    Just execute the specified unit (see next option). This option can be repeated."
        "--verbose          Increases verbosity."
        "--list-tests       List all the available test units."
        "--only <test>      Just execute the specified test by test name or tests that match <test> regexp (if <test> starts with '/'). This option can be repeated."
        "--skip-till <unit> Skip all units until (and including) the specified one."
        "--skipunit <unit>  Skip one unit."
        "--clients <num>    Number of test clients (default 16)."
        "--timeout <sec>    Test timeout in seconds (default 20 min)."
        "--force-failure    Force the execution of a test that always fails."
        "--config <k> <v>   Extra config file argument."
        "--skipfile <file>  Name of a file containing test names or regexp patterns (if <test> starts with '/') that should be skipped (one per line). This option can be repeated."
        "--skiptest <test>  Test name or regexp pattern (if <test> starts with '/') to skip. This option can be repeated."
        "--tags <tags>      Run only tests having specified tags or not having '-' prefixed tags."
        "--dont-clean       Don't delete redis log files after the run."
        "--dont-pre-clean   Don't delete existing redis log files before the run."
        "--no-latency       Skip latency measurements and validation by some tests."
        "--stop             Blocks once the first test fails."
        "--loop             Execute the specified set of tests forever."
        "--loops <count>    Execute the specified set of tests several times."
        "--wait-server      Wait after server is started (so that you can attach a debugger)."
        "--dump-logs        Dump server log on test failure."
        "--tls              Run tests in TLS mode."
        "--tls-module       Run tests in TLS mode with Redis module."
        "--host <addr>      Run tests against an external host."
        "--port <port>      TCP port to use against external host."
        "--baseport <port>  Initial port number for spawned redis servers."
        "--portcount <num>  Port range for spawned redis servers."
        "--singledb         Use a single database, avoid SELECT."
        "--cluster-mode     Run tests in cluster protocol compatible mode."
        "--ignore-encoding  Don't validate object encoding."
        "--ignore-digest    Don't use debug digest validations."
        "--large-memory     Run tests using over 100mb."
        "--help             Print this help screen."
    } "\n"]
}

#parse arguments
proc parse_arguments {argv} {
    for {set j 0} {$j < [llength $argv]} {incr j} {
        set opt [lindex $argv $j]
        set arg [lindex $argv [expr $j+1]]
        if {$opt eq {--client}} {
            set ::client 1
            set ::test_server_port $arg
            incr j
        } elseif {$opt eq {--single}} {
            lappend ::single_tests $arg
            incr j
        } elseif {$opt eq {--dont-pre-clean}} {
            set ::dont_pre_clean 1
        } elseif {$opt eq {--dont-clean}} {
            set ::dont_clean 1
        } elseif {$opt eq {--help}} {
            print_help_screen
            exit 0
        } elseif {$opt eq {--quiet}} {
            set ::quiet 1
        } elseif {$opt eq {--baseport}} {
            set ::baseport $arg
            incr j
        } elseif {$opt eq {--host}} {
            set ::external 1
            set ::host $arg
            incr j
        } elseif {$opt eq {--portcount}} {
            set ::portcount $arg
            incr j
        } else {
            puts "Wrong argument: $opt"
            exit 1
        }
    }
}

proc send_data_packet {fd status data {elapsed 0}} {
    set payload [list $status $data $elapsed]
    puts $fd [string length $payload]
    puts -nonewline $fd $payload
    flush $fd
}

proc execute_tests name {
    set path "tests/$name.tcl"
    set ::curfile $path
    # 执行tcl文件
    source $path
    # 给主线程发送 done 命令
    send_data_packet $::test_server_fd done "$name"
}

proc test_client_main {server_port} {
    # 创建socket 和主线程通信
    set ::test_server_fd [socket localhost $server_port]
    fconfigure $::test_server_fd -encoding binary
    # 发送消息  ready + pid
    send_data_packet $::test_server_fd ready [pid]
    while 1 {
        set bytes [gets $::test_server_fd]
        set payload [read $::test_server_fd $bytes]
        # 等待读取主线程发送的消息 （run tcl文件名）
        foreach {cmd data} $payload break
        if {$cmd eq {run}} {
            execute_tests $data
        } else {
            error "Unknown test client command: $cmd"
        }
    }
}

proc client_main {} {
    if {[catch { test_client_main $::test_server_port } err]} {
        set estr "Executing test client: $err.\n$::errorInfo"
        if {[catch {send_data_packet $::test_server_fd exception $estr}]} {
            puts $estr
        }
        exit 1
    }
}

# 测试运行速度
# With the parallel test running multiple Redis instances at the same time
# we need a fast enough computer, otherwise a lot of tests may generate
# false positives.
# If the computer is too slow we revert the sequential test without any
# parallelism, that is, clients == 1.
proc is_a_slow_computer {} {
    set start [clock milliseconds]
    for {set j 0} {$j < 1000000} {incr j} {}
    set elapsed [expr [clock milliseconds]-$start]
    expr {$elapsed > 200}
}

proc test_server_cron {} {

}

# The the_end function gets called when all the test units were already
# executed, so the test finished.
proc the_end {} {
    # TODO: print the status, exit with the right exit code.
    puts "\n                   The End\n"
    puts "Execution time of different units:"
    foreach {time name} $::clients_time_history {
        puts "  $time seconds - $name"
    }
    if {[llength $::failed_tests]} {
        puts "\n[colorstr bold-red {!!! WARNING}] The following tests failed:\n"
        foreach failed $::failed_tests {
            puts "*** $failed"
        }
        if {!$::dont_clean} cleanup
        exit 1
    } else {
        puts "\n[colorstr bold-white {\o/}] [colorstr bold-green {All tests passed without errors!}]\n"
        if {!$::dont_clean} cleanup
        exit 0
    }
}

# A new client is idle. Remove it from the list of active clients and
# if there are still test units to run, launch them.
# 一个新的客户端处于空闲状态。将其从活动客户端列表中删除并
# 如果还有测试单元要运行，启动它们。
proc signal_idle_client fd {
    # Remove this fd from the list of active clients.
    set ::active_clients \
        [lsearch -all -inline -not -exact $::active_clients $fd]

    # New unit to process?
    if {$::next_test != [llength $::all_tests]} {
        if {!$::quiet} {
            puts [colorstr bold-white "Testing [lindex $::all_tests $::next_test]"]
            set ::active_clients_task($fd) "ASSIGNED: $fd ([lindex $::all_tests $::next_test])"
        }
        set ::clients_start_time($fd) [clock seconds]
        send_data_packet $fd run [lindex $::all_tests $::next_test]
        lappend ::active_clients $fd
        incr ::next_test
        if {$::loop && $::next_test == [llength $::all_tests]} {
            set ::next_test 0
            incr ::loop -1
        }
    } elseif {[llength $::run_solo_tests] != 0 && [llength $::active_clients] == 0} {
        if {!$::quiet} {
            puts [colorstr bold-white "Testing solo test"]
            set ::active_clients_task($fd) "ASSIGNED: $fd solo test"
        }
        set ::clients_start_time($fd) [clock seconds]
        send_data_packet $fd run_code [lpop ::run_solo_tests]
        lappend ::active_clients $fd
    } else {
        lappend ::idle_clients $fd
        set ::active_clients_task($fd) "SLEEPING, no more units to assign"
        if {[llength $::active_clients] == 0} {
            the_end
        }
    }
}

proc kill_clients {} {
    foreach p $::clients_pids {
        catch {exec kill $p}
    }
}

proc force_kill_all_servers {} {
    foreach p $::active_servers {
        puts "Killing still running Redis server $p"
        catch {exec kill -9 $p}
    }
}

# This is the readable handler of our test server. Clients send us messages
# in the form of a status code such and additional data. Supported
# status types are:
#
# ready: the client is ready to execute the command. Only sent at client
#        startup. The server will queue the client FD in the list of idle
#        clients. (客户端已准备好执行命令。仅在客户端启动时发送。服务器会将客户端FD排队到空闲客户端列表中)
# testing: just used to signal that a given test started.
# ok: a test was executed with success.
# err: a test was executed with an error.
# skip: a test was skipped by skipfile or individual test options.
# ignore: a test was skipped by a group tag.
# exception: there was a runtime exception while executing the test.
# done: all the specified test file was processed, this test client is
#       ready to accept a new task.
proc read_from_test_client fd {
    set bytes [gets $fd]
    set payload [read $fd $bytes]
    foreach {status data elapsed} $payload break
    set ::last_progress [clock seconds]

    if {$status eq {ready}} {
        if {!$::quiet} {
            puts "\[$status\]: $data"
        }
        signal_idle_client $fd
    } elseif {$status eq {done}} {
        set elapsed [expr {[clock seconds]-$::clients_start_time($fd)}]
        set all_tests_count [llength $::all_tests]
        set running_tests_count [expr {[llength $::active_clients]-1}]
        set completed_tests_count [expr {$::next_test-$running_tests_count}]
        puts "\[$completed_tests_count/$all_tests_count [colorstr yellow $status]\]: $data ($elapsed seconds)"
        lappend ::clients_time_history $elapsed $data
        signal_idle_client $fd
        set ::active_clients_task($fd) "(DONE) $data"
    } elseif {$status eq {ok}} {
        if {!$::quiet} {
            puts "\[[colorstr green $status]\]: $data ($elapsed ms)"
        }
        set ::active_clients_task($fd) "(OK) $data"
    } elseif {$status eq {skip}} {
        if {!$::quiet} {
            puts "\[[colorstr yellow $status]\]: $data"
        }
    } elseif {$status eq {ignore}} {
        if {!$::quiet} {
            puts "\[[colorstr cyan $status]\]: $data"
        }
    } elseif {$status eq {err}} {
        set err "\[[colorstr red $status]\]: $data"
        puts $err
        lappend ::failed_tests $err
        set ::active_clients_task($fd) "(ERR) $data"
        if {$::stop_on_failure} {
            puts -nonewline "(Test stopped, press enter to resume the tests)"
            flush stdout
            gets stdin
        }
    } elseif {$status eq {exception}} {
        puts "\[[colorstr red $status]\]: $data"
        kill_clients
        force_kill_all_servers
        exit 1
    } elseif {$status eq {testing}} {
        set ::active_clients_task($fd) "(IN PROGRESS) $data"
    } elseif {$status eq {server-spawning}} {
        set ::active_clients_task($fd) "(SPAWNING SERVER) $data"
    } elseif {$status eq {server-spawned}} {
        lappend ::active_servers $data
        set ::active_clients_task($fd) "(SPAWNED SERVER) pid:$data"
    } elseif {$status eq {server-killing}} {
        set ::active_clients_task($fd) "(KILLING SERVER) pid:$data"
    } elseif {$status eq {server-killed}} {
        set ::active_servers [lsearch -all -inline -not -exact $::active_servers $data]
        set ::active_clients_task($fd) "(KILLED SERVER) pid:$data"
    } elseif {$status eq {run_solo}} {
        lappend ::run_solo_tests $data
    } else {
        if {!$::quiet} {
            puts "\[$status\]: $data"
        }
    }
}

proc accept_test_clients {fd addr port} {
    fconfigure $fd -encoding binary
    fileevent $fd readable [list read_from_test_client $fd]
}

proc test_server_main {} {
    # 清理文件
    if {!$::dont_pre_clean} cleanup
    # 获得tcl执行文件
    set tclsh [info nameofexecutable]

    set clientport [find_available_port [expr {$::baseport - 32}] 32]
    if {!$::quiet} {
        puts "Starting test server at port $clientport"
    }
    socket -server accept_test_clients  -myaddr 127.0.0.1 $clientport

    # Start the client instances
    set ::clients_pids {}
    if {$::external} {
        set p [exec $tclsh [info script] {*}$::argv \
            --client $clientport &]
        lappend ::clients_pids $p
    } else {
        set start_port $::baseport
        set port_count [expr {$::portcount / $::numclients}]
        for {set j 0} {$j < $::numclients} {incr j} {
            set p [exec $tclsh [info script] {*}$::argv \
                --client $clientport --baseport $start_port --portcount $port_count &]
            lappend ::clients_pids $p
            incr start_port $port_count
        }
    }

    # Setup global state for the test server
    set ::idle_clients {}
    set ::active_clients {}
    array set ::active_clients_task {}
    array set ::clients_start_time {}
    set ::clients_time_history {}
    set ::failed_tests {}

    # Enter the event loop to handle clients I/O
    after 100 test_server_cron
    vwait forever
}
proc server_main {} {
    if {[is_a_slow_computer]} {
        puts "** SLOW COMPUTER ** Using a single client to avoid false positives."
        set ::numclients 1
    }

    if {[catch { test_server_main } err]} {
        if {[string length $err] > 0} {
            # only display error when not generated by the test suite
            if {$err ne "exception"} {
                puts $::errorInfo
            }
            exit 1
        }
    }
}
proc main {argv} {
    parse_arguments $argv
    if {$::client} {
        client_main
    } else {
        server_main
    }
}

main $argv