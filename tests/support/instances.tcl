
# 记录pid
set ::pids {} ; # We kill everything at exit
# 记录文件夹
set ::dirs {} ; # We remove all the temp dirs at exit


proc log_crashes {} {
    set start_pattern {*REDIS BUG REPORT START*}
    set logs [glob */log.txt]
    foreach log $logs {
        set fd [open $log]
        set found 0
        while {[gets $fd line] >= 0} {
            if {[string match $start_pattern $line]} {
                puts "\n*** Crash report found in $log ***"
                set found 1
            }
            if {$found} {
                puts $line
                incr ::failed
            }
        }
    }

    set logs [glob */err.txt]
    foreach log $logs {
        set res [find_valgrind_errors $log true]
        if {$res != ""} {
            puts $res
            incr ::failed
        }
    }

    set logs [glob */err.txt]
    foreach log $logs {
        set res [sanitizer_errors_from_file $log]
        if {$res != ""} {
            puts $res
            incr ::failed
        }
    }
}

proc cleanup {} {
    puts "Cleaning up..."
    foreach pid $::pids {
        puts "killing stale instance $pid"
        stop_instance $pid
    }
    # log_crashes
    if {$::dont_clean} {
        return
    }
    foreach dir $::dirs {
        catch {exec rm -rf $dir}
    }
}


# about test

# We redefine 'test' as for Sentinel we don't use the server-client
# architecture for the test, everything is sequential.
proc test {descr code} {
    set ts [clock format [clock seconds] -format %H:%M:%S]
    puts -nonewline "$ts> $descr: "
    flush stdout

    if {[catch {set retval [uplevel 1 $code]} error]} {
        incr ::failed
        if {[string match "assertion:*" $error]} {
            set msg "FAILED: [string range $error 10 end]"
            puts [colorstr red $msg]
            if {$::pause_on_error} pause_on_error
            puts [colorstr red "(Jumping to next unit after error)"]
            return -code continue
        } else {
            # Re-raise, let handler up the stack take care of this.
            error $error $::errorInfo
        }
    } else {
        puts [colorstr green OK]
    }
}