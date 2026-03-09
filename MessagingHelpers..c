///////////////////////////////////////////////////////////////////////////
//   MessagingHelpers.c
//   College of Applied Science and Technology
//   The University of Arizona
//   CYBV 489
//   Student Names:  Dean Lewis
///////////////////////////////////////////////////////////////////////////
#include <Windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <THREADSLib.h>
#include <Scheduler.h>
#include <Messaging.h>
#include <stdint.h>
#include "message.h"
#include "MessagingHelpers.h"

//////////// USER GLOBAL VARIABLES ////////////
SlotPtr freeSlotHead = NULL;                 // Slot free list management   TEST02 ADD
int g_mailbox_maxSlots[MAXMBOX];             // TEST03 ADD
MsgProcEntry g_msgProc[MAXPROC];             // TEST05 ADD One waiting node per process
WaitingProcess g_waitNode[MAXPROC];          // TEST05 ADD
WaitingProcessPtr g_waitRecvHead[MAXMBOX];   // TEST05 ADD mailbox wait queues receiver head
WaitingProcessPtr g_waitRecvTail[MAXMBOX];   // TEST05 ADD mailbox wait queues receiver tail
WaitingProcessPtr g_waitSendHead[MAXMBOX];   // TEST05 ADD mailbox wait queues sender head
WaitingProcessPtr g_waitSendTail[MAXMBOX];   // TEST05 ADD mailbox wait queues sender tail
SlotPtr g_slotTail[MAXMBOX];                 // TEST05 ADD mailbox slot tail for FIFO
///////////////////////////////////////////////

static int mpIndex(int pid);                // TEST05 ADD

static int mpIndex(int pid)
{
    // TEST09 ADD DO NOT use pid % MAXPROC (pid collisions kill the wrong process)

    // 1) If PID already has a slot, return it
    for (int i = 0; i < MAXPROC; i++)
    {
        if (g_msgProc[i].pid == pid)
            return i;
    }

    // 2) Otherwise allocate a free slot
    for (int i = 0; i < MAXPROC; i++)
    {
        if (g_msgProc[i].pid == -1)
        {
            g_msgProc[i].pid = pid;

            // Initialize process messaging state
            g_msgProc[i].recvBuf = NULL;
            g_msgProc[i].recvMax = 0;
            g_msgProc[i].recvResult = -9999;

            g_msgProc[i].sendBuf = NULL;
            g_msgProc[i].sendSize = 0;
            g_msgProc[i].sendResult = -9999;

            g_msgProc[i].blockedMbox = -1;
            g_msgProc[i].blockedType = 0;

            // Also initialize wait node for safety
            g_waitNode[i].pid = pid;
            g_waitNode[i].pNextProcess = NULL;
            g_waitNode[i].pPrevProcess = NULL;

            return i;
        }
    }

    // No room (shouldn't happen in these tests)
    return -1;
}

void init_proc_table(void)
{
    for (int i = 0; i < MAXPROC; i++)
    {
        g_msgProc[i].pid = -1;
        g_msgProc[i].recvBuf = NULL;
        g_msgProc[i].recvMax = 0;
        g_msgProc[i].recvResult = -9999;
        g_msgProc[i].sendBuf = NULL;
        g_msgProc[i].sendSize = 0;
        g_msgProc[i].sendResult = -9999;
        g_msgProc[i].blockedMbox = -1;
        g_msgProc[i].blockedType = 0;

        g_waitNode[i].pid = -1;
        g_waitNode[i].pNextProcess = NULL;
        g_waitNode[i].pPrevProcess = NULL;
    }
}

/* an error method to handle slot free list setup */
void init_slot_freelist(void)
{
    freeSlotHead = NULL;
    for (int i = 0; i < MAXSLOTS; i++)
    {
        mailSlots[i].pNextSlot = freeSlotHead;
        mailSlots[i].pPrevSlot = NULL;
        mailSlots[i].mbox_id = -1;
        mailSlots[i].messageSize = 0;
        freeSlotHead = &mailSlots[i];
    }
}

// allocate slot
SlotPtr allocate_slot(void)
{
    SlotPtr _slotptr = freeSlotHead;
    if (_slotptr != NULL)
    {
        freeSlotHead = _slotptr->pNextSlot;
        if (freeSlotHead) freeSlotHead->pPrevSlot = NULL;

        _slotptr->pNextSlot = NULL;
        _slotptr->pPrevSlot = NULL;
        _slotptr->mbox_id = -1;
        _slotptr->messageSize = 0;
    }
    return _slotptr;
}

// Free slot in list
// Free slot in list
void free_slot(SlotPtr _slotptr)
{
    if (!_slotptr) return;

    /* Push onto free list head */
    _slotptr->pNextSlot = freeSlotHead;
    _slotptr->pPrevSlot = NULL;

    /* Maintain backward link on old head (keeps list consistent) */
    if (freeSlotHead)
    {
        freeSlotHead->pPrevSlot = _slotptr;
    }

    _slotptr->mbox_id = -1;
    _slotptr->messageSize = 0;

    freeSlotHead = _slotptr;
}

// Initialize empty mailboxes
void init_mailboxes(void)
{
    for (int i = 0; i < MAXMBOX; i++)
    {
        mailboxes[i].pSlotListHead = NULL;
        mailboxes[i].mbox_id = i;
        mailboxes[i].type = MB_MAXTYPES;            // unknown until create
        mailboxes[i].status = MBSTATUS_EMPTY;       // Empty Slot
        mailboxes[i].slotSize = 0;
        mailboxes[i].slotCount = 0;

        g_mailbox_maxSlots[i] = 0;                  // TEST03 ADD: initialize maxSlots array to 0 for all mailboxes

        g_slotTail[i] = NULL;                       // TEST05 ADD initialize slot, send, receive head and tails
        g_waitRecvHead[i] = NULL;
        g_waitRecvTail[i] = NULL;
        g_waitSendHead[i] = NULL;
        g_waitSendTail[i] = NULL;
    }
}

void waitq_push(WaitingProcessPtr* head, WaitingProcessPtr* tail, WaitingProcessPtr n) {
    n->pNextProcess = NULL;
    n->pPrevProcess = *tail;
    if (*tail) (*tail)->pNextProcess = n;
    else *head = n;
    *tail = n;
}

WaitingProcessPtr waitq_pop(WaitingProcessPtr* head, WaitingProcessPtr* tail) {
    WaitingProcessPtr n = *head;
    if (!n) return NULL;
    *head = n->pNextProcess;
    if (*head) (*head)->pPrevProcess = NULL;
    else *tail = NULL;
    n->pNextProcess = n->pPrevProcess = NULL;
    return n;
}

void slot_enqueue(int mboxId, SlotPtr s) {
    MailBox* m = &mailboxes[mboxId];
    s->pNextSlot = NULL;
    s->pPrevSlot = g_slotTail[mboxId];
    if (g_slotTail[mboxId]) g_slotTail[mboxId]->pNextSlot = s;
    else m->pSlotListHead = s;
    g_slotTail[mboxId] = s;
}

SlotPtr slot_dequeue(int mboxId) {
    MailBox* m = &mailboxes[mboxId];
    SlotPtr s = m->pSlotListHead;
    if (!s) return NULL;
    m->pSlotListHead = s->pNextSlot;
    if (m->pSlotListHead) m->pSlotListHead->pPrevSlot = NULL;
    else g_slotTail[mboxId] = NULL;
    s->pNextSlot = s->pPrevSlot = NULL;
    return s;
}

// Handle pointer values as numeric     // TEST05 ADD - Fix exception in io_handler
int device_id_from_param(char deviceId[32])
{
    /* In this THREADS build, the first param is effectively a device index.
       It may be passed as a small integer cast to a pointer. */

    uintptr_t raw = (uintptr_t)deviceId;

    /* If it's a small value, treat it as the device index (expected case). */
    if (raw < (uintptr_t)THREADS_MAX_DEVICES)
        return (int)raw;

    /* Otherwise, it's not a valid device index in this build. */
    return -1;
}

/* TEST05 ADD: proc table helpers */
/* TEST09 FIX proc table helpers */
MsgProcEntry* mp_for_pid(int pid)
{
    int idx = mpIndex(pid);
    if (idx < 0) return NULL;
    return &g_msgProc[idx];
}

MsgProcEntry* mp_self(void)
{
    return mp_for_pid(k_getpid());
}

WaitingProcessPtr wp_for_pid(int pid)
{
    int idx = mpIndex(pid);
    if (idx < 0) return NULL;

    g_waitNode[idx].pid = pid;
    g_waitNode[idx].pNextProcess = NULL;
    g_waitNode[idx].pPrevProcess = NULL;
    return &g_waitNode[idx];
}

void prepare_blocked_sender(MsgProcEntry* me, int mboxId, void* pMsg, int msg_size)
{
    me->sendBuf = pMsg;
    me->sendSize = msg_size;
    me->sendResult = -9999;
    me->blockedMbox = mboxId;
    me->blockedType = BLOCKED_SEND;
}

void prepare_blocked_receiver(MsgProcEntry* me, int mboxId, void* pMsg, int msg_size)
{
    me->recvBuf = pMsg;
    me->recvMax = msg_size;
    me->recvResult = -9999;
    me->blockedMbox = mboxId;
    me->blockedType = BLOCKED_RECEIVE;
}

int finish_blocked_call(MsgProcEntry* me, int result)
{
    me->blockedMbox = -1;
    me->blockedType = 0;
    enableInterrupts();
    return result;
}