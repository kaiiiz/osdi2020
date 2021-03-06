#define __ASSEMBLY__
#include "sys.h"

.global get_taskid
get_taskid:
    mov x8, SYS_GET_TASK_ID
    svc #0
    ret

.global uart_read
uart_read:
    mov x8, SYS_UART_READ
    svc #0
    ret

.global uart_write
uart_write:
    mov x8, SYS_UART_WRITE
    svc #0
    ret

.global exec
exec:
    mov x8, SYS_EXEC
    svc #0
    ret

.global fork
fork:
    mov x8, SYS_FORK
    svc #0
    ret

.global exit
exit:
    mov x8, SYS_EXIT
    svc #0
    ret

.global remain_page_num
remain_page_num:
    mov x8, SYS_REMAIN_PAGE
    svc #0
    ret

.global open
open:
    mov x8, SYS_OPEN
    svc #0
    ret

.global close
close:
    mov x8, SYS_CLOSE
    svc #0
    ret

.global write
write:
    mov x8, SYS_WRITE
    svc #0
    ret

.global read
read:
    mov x8, SYS_READ
    svc #0
    ret

.global readdir
readdir:
    mov x8, SYS_READDIR
    svc #0
    ret

.global mkdir
mkdir:
    mov x8, SYS_MKDIR
    svc #0
    ret

.global chdir
chdir:
    mov x8, SYS_CHDIR
    svc #0
    ret

.global mount
mount:
    mov x8, SYS_MOUNT
    svc #0
    ret

.global umount
umount:
    mov x8, SYS_UMOUNT
    svc #0
    ret
