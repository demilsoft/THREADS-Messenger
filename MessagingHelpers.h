#pragma once
#pragma once

#include <stdint.h>
#include <THREADSLib.h>
#include <Scheduler.h>
#include <Messaging.h>
#include "message.h"

/* ------------------------- Internal Types -------------------------------- */

/* TEST05 ADD Messaging internal state */
typedef struct {
    int pid;
    void* recvBuf;
    int recvMax;
    int recvResult;

    void* sendBuf;
    int sendSize;
    int sendResult;

    int blockedMbox;      // TEST09 ADD
    int blockedType;      // TEST09 ADD
} MsgProcEntry;

/* ------------------------- Extern Globals -------------------------------- */

/* These are defined in Messaging.c */
extern MailBox mailboxes[MAXMBOX];
extern MailSlot mailSlots[MAXSLOTS];

/* These are defined in MessagingHelpers.c */
extern SlotPtr freeSlotHead;                 // Slot free list management   TEST02 ADD
extern int g_mailbox_maxSlots[MAXMBOX];      // TEST03 ADD
extern MsgProcEntry g_msgProc[MAXPROC];      // TEST05 ADD One waiting node per process
extern WaitingProcess g_waitNode[MAXPROC];   // TEST05 ADD
extern WaitingProcessPtr g_waitRecvHead[MAXMBOX];   // TEST05 ADD mailbox wait queues receiver head
extern WaitingProcessPtr g_waitRecvTail[MAXMBOX];   // TEST05 ADD mailbox wait queues receiver tail
extern WaitingProcessPtr g_waitSendHead[MAXMBOX];   // TEST05 ADD mailbox wait queues sender head
extern WaitingProcessPtr g_waitSendTail[MAXMBOX];   // TEST05 ADD mailbox wait queues sender tail
extern SlotPtr g_slotTail[MAXMBOX];          // TEST05 ADD mailbox slot tail for FIFO

/* ------------------------- Helper Prototypes ----------------------------- */

void init_slot_freelist(void);       // TEST03 ADD
SlotPtr allocate_slot(void);         // TEST03 ADD
void free_slot(SlotPtr s);           // TEST03 ADD
void init_mailboxes(void);           // TEST03 ADD
void init_proc_table(void);          // CLEANUP ADD

void waitq_push(WaitingProcessPtr* head, WaitingProcessPtr* tail, WaitingProcessPtr n);       // TEST05 ADD
WaitingProcessPtr waitq_pop(WaitingProcessPtr* head, WaitingProcessPtr* tail);                // TEST05 ADD
void slot_enqueue(int mboxId, SlotPtr s);                                                     // TEST05 ADD
SlotPtr slot_dequeue(int mboxId);                                                             // TEST05 ADD

int device_id_from_param(char deviceId[32]);                                                  // TEST05 FIX ADD
MsgProcEntry* mp_for_pid(int pid);           // TEST10 ADD helper to get MsgProcEntry pointer for a given pid
MsgProcEntry* mp_self(void);                 // TEST10 ADD helper to get current process's MsgProcEntry pointer
WaitingProcessPtr wp_for_pid(int pid);       // TEST10 ADD helper to get WaitingProcessPtr for a given pid

void prepare_blocked_sender(MsgProcEntry* me, int mboxId, void* pMsg, int msg_size);          // CLEANUP ADD
void prepare_blocked_receiver(MsgProcEntry* me, int mboxId, void* pMsg, int msg_size);        // CLEANUP ADD
int finish_blocked_call(MsgProcEntry* me, int result);                                         // CLEANUP ADD