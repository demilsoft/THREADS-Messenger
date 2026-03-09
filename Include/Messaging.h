#pragma once

#include <Windows.h>
#include <THREADSLib.h>

#define MAXLINE         80      /* 80 Maximum line length. Used by terminal read and write */
#define MAXMBOX         2000    /* 500 */
#define MAXSLOTS        2500    /* 5000 */
#define MAX_MESSAGE     256     /* largest possible message in a single slot */

/* returns id of mailbox, or -1 if no more mailboxes or error */
int mailbox_create(int slots, int slot_size);

/* returns 0 if successful, -1 if invalid arg */
extern int mailbox_free(int mbox_id);

/* returns 0 if successful, -1 if invalid args */
extern int mailbox_send(int mbox_id, void* msg_ptr, int msg_size, BOOL block);

/* returns size of received msg if successful, -1 if invalid args */
extern int mailbox_receive(int mbox_id, void* msg_ptr, int msg_max_size, BOOL block);

/* type = interrupt device type, unit = # of device (when more than one),
 * status = where interrupt handler puts device's status register.
 */
extern int wait_device(char* deviceName, int* status);