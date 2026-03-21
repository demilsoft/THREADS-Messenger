///////////////////////////////////////////////////////////////////////////
//   MessagingHelpers.h
//   College of Applied Science and Technology
//   The University of Arizona
//   CYBV 489
//   Student Names:  Dean Lewis
///////////////////////////////////////////////////////////////////////////
#pragma once
#pragma once

#include <stdint.h>
#include <THREADSLib.h>
#include <Scheduler.h>
#include "Messaging.h"
#include "message.h"

// TEST05 ADD Messaging internal state
typedef struct {
    int pid;
    void* recvBuf;
    int recvMax;
    int recvResult;
    void* sendBuf;
    int sendSize;
    int sendResult;
    int blockedMbox;                                // TEST09 ADD mailbox currently blocking process
    int blockedType;                                // TEST09 ADD blocked send or receive type
} MsgProcEntry;

//////////////////////////////////// GLOBALS g_ //////////////////////////////////////
extern MailBox mailboxes[MAXMBOX];
extern MailSlot mailSlots[MAXSLOTS];
extern SlotPtr g_freeSlotHead;                      // TEST02 ADD Slot free list management  
extern int g_mailbox_maxSlots[MAXMBOX];             // TEST03 ADD max slots per mailbox
extern MsgProcEntry g_msgProc[MAXPROC];             // TEST05 ADD One waiting node per process
extern WaitingProcess g_waitNode[MAXPROC];          // TEST05 ADD wait queue nodes per process
extern WaitingProcessPtr g_waitRecvHead[MAXMBOX];   // TEST05 ADD mailbox wait queues receiver head
extern WaitingProcessPtr g_waitRecvTail[MAXMBOX];   // TEST05 ADD mailbox wait queues receiver tail
extern WaitingProcessPtr g_waitSendHead[MAXMBOX];   // TEST05 ADD mailbox wait queues sender head
extern WaitingProcessPtr g_waitSendTail[MAXMBOX];   // TEST05 ADD mailbox wait queues sender tail
extern SlotPtr g_slotTail[MAXMBOX];                 // TEST05 ADD mailbox slot tail
extern int g_releaseWaitCount[MAXMBOX];             // TEST17 ADD how many awakened waiters still need to finish
extern int g_releaseFreerPid[MAXMBOX];              // TEST17 ADD pid of process inside mailbox_free waiting 
//////////////////////////////////// GLOBALS g_ //////////////////////////////////////

//////////////////////////////////// PROTOTYPES //////////////////////////////////////
void init_slot_freelist(void);                      // TEST03 ADD init slot free list
SlotPtr allocate_free_slot(void);                   // TEST03 ADD get free mail slot
void return_free_slot(SlotPtr s);                   // TEST03 ADD return slot to free list
void init_mailboxes(void);                          // TEST03 ADD init mailbox table
void init_proc_table(void);                         // TEST25 ADD init process messaging table

void wait_queue_push(WaitingProcessPtr* head, WaitingProcessPtr* tail, WaitingProcessPtr n);       // TEST05 ADD add process to wait queue
WaitingProcessPtr wait_queue_pop(WaitingProcessPtr* head, WaitingProcessPtr* tail);                // TEST05 ADD remove process from wait queue
void slot_enqueue(int mboxId, SlotPtr s);                                                          // TEST05 ADD add slot to mailbox queue
SlotPtr slot_dequeue(int mboxId);                                                                  // TEST05 ADD remove slot from mailbox queue

int device_id_from_param(char deviceId[32]);        // TEST05 FIX ADD
MsgProcEntry* mpe_for_pid(int pid);                 // TEST10 ADD helper to get MsgProcEntry pointer for a given pid
MsgProcEntry* mpe_self(void);                       // TEST10 ADD helper to get current process MsgProcEntry pointer
WaitingProcessPtr wp_for_pid(int pid);              // TEST10 ADD helper to get WaitingProcessPtr for a given pid

void prepare_blocked_sender(MsgProcEntry* me, int mboxId, void* pMsg, int msg_size);          // TEST25 ADD save blocked sender state
void prepare_blocked_receiver(MsgProcEntry* me, int mboxId, void* pMsg, int msg_size);        // TEST25 ADD save blocked receiver state
int finish_blocked_call(MsgProcEntry* me, int result);                                        // TEST25 ADD clear blocked process state
//////////////////////////////////// PROTOTYPES //////////////////////////////////////