#pragma once

#include <Windows.h>
#include <THREADSLib.h>

#define MAXLINE         80      /* 80 Maximum line length. Used by terminal read and write */
#define MAXMBOX         2000    /* 500 */
#define MAXSLOTS        2500    /* 5000 */
#define MAX_MESSAGE     256     /* largest possible message in a single slot */

int mailbox_create(int slots, int slot_size);
extern int mailbox_free(int mbox_id);
extern int mailbox_send(int mbox_id, void* msg_ptr, int msg_size, BOOL block);
extern int mailbox_receive(int mbox_id, void* msg_ptr, int msg_max_size, BOOL block);
/* type = interrupt device type, unit = # of device (when more than one),
 * status = where interrupt handler puts device's status register.*/
extern int wait_device(char* deviceName, int* status);