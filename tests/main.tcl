package require Tcl 8.5

set tcl_precision 17
source tests/support/client.tcl
source tests/support/server.tcl 
source tests/support/util.tcl 

#黑盒测试
set ::all_tests {

}

proc main {argv all_tests} {
    for {set j 0} {$j < [llength $argv]} {incr j} {
        puts [lindex $argv $j]
    }
}

# execute
main $argv $all_tests 




 