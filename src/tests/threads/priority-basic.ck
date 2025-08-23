# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(priority-basic) begin
(priority-basic) Creating threads with different priorities...
(priority-basic) All threads created and waiting. Now releasing them simultaneously...
(priority-basic) High priority thread (priority 34) running
(priority-basic) Medium priority thread (priority 33) running
(priority-basic) Low priority thread (priority 32) running
(priority-basic) Execution order: 34, 33, 32
(priority-basic) Basic priority scheduling works correctly!
(priority-basic) end
EOF
pass;
