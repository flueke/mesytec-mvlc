#
#  This file is a package index that will load the 
#  slow slow controls library on 
#   package require vmlcvme ?1.0?

package ifneeded mvlcvme 1.0 [list load [file join $dir libslowControlsClient.so]]