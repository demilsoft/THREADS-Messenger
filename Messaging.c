///////////////////////////////////////////////////////////////////////////
//   Messaging.c
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

/* -------------------------- Globals ------------------------------------- */
interrupt_handler_t* handlers;      // Obtained from THREADS
void (*systemCallVector[THREADS_MAX_SYSCALLS])(system_call_arguments_t* args); // system call array of function pointers 
MailBox mailboxes[MAXMBOX];         // The mail boxes 
MailSlot mailSlots[MAXSLOTS];       // The mail slots 

struct psr_bits {
    unsigned int cur_int_enable : 1;
    unsigned int cur_mode : 1;
    unsigned int prev_int_enable : 1;
    unsigned int prev_mode : 1;
    unsigned int unused : 28;
};

union psr_values {
    struct psr_bits bits;
    unsigned int integer_part;
};

typedef struct
{
    void* deviceHandle;
    int deviceMbox;
    int deviceType;
    char deviceName[16];
} DeviceManagementData;

// TEST05 ADD Messaging internal state
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

static DeviceManagementData devices[THREADS_MAX_DEVICES];
static int nextMailboxId = 0;
static int waitingOnDevice = 0;

//////////// USER GLOBAL VARIABLES ////////////
static int g_mailbox_inuse[MAXMBOX];                // 0 = Free, 1 = Used   TEST02 ADD
static int g_mailbox_slots[MAXMBOX];                // MAX slots requested   TEST02 ADD 
static int g_mailbox_slot_size[MAXMBOX];            // MAX bytes per message    TEST02 ADD
static SlotPtr freeSlotHead = NULL;                 // Slot free list management   TEST02 ADD
static int g_mailbox_maxSlots[MAXMBOX];             // TEST03 ADD
static MsgProcEntry g_msgProc[MAXPROC];             // TEST05 ADD One waiting node per process
static WaitingProcess g_waitNode[MAXPROC];          // TEST05 ADD
static WaitingProcessPtr g_waitRecvHead[MAXMBOX];   // TEST05 ADD mailbox wait queues receiver head
static WaitingProcessPtr g_waitRecvTail[MAXMBOX];   // TEST05 ADD mailbox wait queues receiver tail
static WaitingProcessPtr g_waitSendHead[MAXMBOX];   // TEST05 ADD mailbox wait queues sender head
static WaitingProcessPtr g_waitSendTail[MAXMBOX];   // TEST05 ADD mailbox wait queues sender tail
static SlotPtr g_slotTail[MAXMBOX];                 // TEST05 ADD mailbox slot tail for FIFO
static int g_releaseWaitCount[MAXMBOX];             // TEST09 ADD how many awakened waiters still need to exit
static int g_releaseFreerPid[MAXMBOX];              // TEST09 ADD pid of process inside mailbox_free waiting to resume
///////////////////////////////////////////////
/* -------------------------- Globals ------------------------------------- */

/* ------------------------- Prototypes ----------------------------------- */
static void nullsys(system_call_arguments_t* args);
/* Note: interrupt_handler_t is already defined in THREADSLib.h with the signature:
 *   void (*)(char deviceId[32], uint8_t command, uint32_t status, void *pArgs) */
static void InitializeHandlers();
static int check_io_messaging(void);
extern int MessagingEntryPoint(void*);
static void checkKernelMode(const char* functionName);
/* ------------------------- Prototypes ----------------------------------- */
//////////////////// HELPER FUNCTION PROTOTYPES /////////////////////////
static void init_slot_freelist(void);       // TEST03 ADD
static SlotPtr allocate_slot(void);         // TEST03 ADD
static void free_slot(SlotPtr s);           // TEST03 ADD
static void init_mailboxes(void);           // TEST03 ADD
static int mpIndex(int pid);                // TEST05 ADD
static void waitq_push(WaitingProcessPtr* head, WaitingProcessPtr* tail, WaitingProcessPtr n);       // TEST05 ADD
static WaitingProcessPtr waitq_pop(WaitingProcessPtr* head, WaitingProcessPtr* tail);                // TEST05 ADD
static void slot_enqueue(int mboxId, SlotPtr s);                                                     // TEST05 ADD
static SlotPtr slot_dequeue(int mboxId);                                                             // TEST05 ADD
static void io_handler(char deviceId[32], uint8_t command, uint32_t status, void* pArgs);            // TEST05 ADD
static void syscall_handler(char deviceId[32], uint8_t command, uint32_t status, void* pArgs);       // TEST05 ADD
static int find_device_index_by_name(const char* name);                                              // TEST05 ADD
static inline int device_id_from_param(char deviceId[32]);                                           // TEST05 FIX ADD
static MsgProcEntry* mp_for_pid(int pid);           // TEST10 ADD helper to get MsgProcEntry pointer for a given pid
static MsgProcEntry* mp_self(void);                 // TEST10 ADD helper to get current process's MsgProcEntry pointer
static WaitingProcessPtr wp_for_pid(int pid);       // TEST10 ADD helper to get WaitingProcessPtr for a given pid 
static void clock_handler_messaging(char deviceId[32], uint8_t command, uint32_t status, void* pArgs);  // TEST08 ADD
/////////////////////////////////////////////////////////////////////////

int SchedulerEntryPoint(void* arg)
{
    // TEST03 ADD check for kernel mode
    checkKernelMode("SchedulerEntryPoint");

    /* Disable interrupts */
    disableInterrupts();

    /* set this to the real check_io function. */
    check_io = check_io_messaging;

    /* Initialize the mail box table, slots, & other data structures.
     * Initialize int_vec and sys_vec, allocate mailboxes for interrupt
     * handlers.  Etc... */

     /* Initialize the devices and their mailboxes. */
     /* Allocate mailboxes for use by the interrupt handlers.
      * Note: The clock device uses a zero-slot mailbox, while I/O devices
      * (disks, terminals) need slotted mailboxes since their interrupt
      * handlers use non-blocking sends.
      */

      // TEST03 ADD: init mailbox/slot structures first so mailbox_create works cleanly.
    init_mailboxes();           // TEST03 ADD

    // MAKE SURE TO INITIALIZE YOUR DEVICE MANAGEMENT DATA STRUCTURE
    for (int i = 0; i < THREADS_MAX_DEVICES; i++) {
        devices[i].deviceHandle = NULL;
        devices[i].deviceMbox = -1;
        devices[i].deviceType = 0;
        devices[i].deviceName[0] = '\0';        // NULL TERM
    }

    init_slot_freelist();       // TEST03 ADD

    ///////////////////////////////////////////////////////////////////////////////////////////////
    /* TODO: Initialize the devices using device_initialize().
     * The devices are: disk0, disk1, term0, term1, term2, term3.
     * Store the device handle and name in the devices array.
     */

     // Create device mailboxes
     // TODO: Create mailboxes for each device.
     //   devices[THREADS_CLOCK_DEVICE_ID].deviceMbox = mailbox_create(0, sizeof(int));      // TEST05 ADD

     // NOTE (per lecture):
     // We create 1 clock mailbox + 2 disk mailboxes + 4 terminal mailboxes (7 total),
     // then create ONE extra mailbox so mailbox IDs align with instructor output.      // TEST09 ADD

     // TEST08 FIX CLOCK fixed device index */
    devices[THREADS_CLOCK_DEVICE_ID].deviceHandle = (void*)(uintptr_t)THREADS_CLOCK_DEVICE_ID;
    strncpy(devices[THREADS_CLOCK_DEVICE_ID].deviceName, "clock", sizeof(devices[0].deviceName) - 1);

    // TEST08 FIX Clock device uses a zero-slot mailbox */
    devices[THREADS_CLOCK_DEVICE_ID].deviceMbox = mailbox_create(0, sizeof(int));
    if (devices[THREADS_CLOCK_DEVICE_ID].deviceMbox < 0) {
        console_output(FALSE, "SchedulerEntryPoint: mailbox_create(clock) failed\n");
        stop(1);
    }

    // IO devices: slotted mailbox; handler uses non-blocking sends
    //   devices[i].deviceMbox = mailbox_create(..., sizeof(int));            // TEST05 ADD

    // TEST07 ALTER TO LOOP OVER ALL DEVICES AND INITIALIZE
    // TEST08 FIX I/O devices that may interrupt */
    const char* ioDevices[] = { "disk0", "disk1", "term0", "term1", "term2", "term3" };

    for (int d = 0; d < 6; d++)
    {
        int h = device_initialize((char*)ioDevices[d]);
        if (h < 0 || h >= THREADS_MAX_DEVICES)
        {
            console_output(FALSE, "SchedulerEntryPoint: device_initialize(%s) failed (%d)\n", ioDevices[d], h);
            stop(1);
        }

        // Store by HANDLER INDEX
        devices[h].deviceHandle = (void*)(uintptr_t)h;
        strncpy(devices[h].deviceName, ioDevices[d], sizeof(devices[0].deviceName) - 1);

        /* I/O devices need slotted mailbox because interrupt handler uses non-blocking send */
        devices[h].deviceMbox = mailbox_create(10, sizeof(int));
        if (devices[h].deviceMbox < 0)
        {
            console_output(FALSE, "SchedulerEntryPoint: mailbox_create(%s) failed\n", ioDevices[d]);
            stop(1);
        }
    }

    // TEST09 ADD (per lecture): create one extra mailbox to align IDs with instructor output
    // (Instructor created one extra mailbox accidentally; this keeps our code flexible while matching output.)
    (void)mailbox_create(1, sizeof(int));

    ///////////////////////////////////////////////////////////////////////////////////////////////

    for (int i = 0; i < MAXPROC; i++) {         // TEST05 ADD initialize proc table
        g_msgProc[i].pid = -1;
        g_waitNode[i].pid = -1;
        g_waitNode[i].pNextProcess = NULL;
        g_waitNode[i].pPrevProcess = NULL;
    }

    InitializeHandlers();
    enableInterrupts();

    /* Spawn the test process (MessagingEntryPoint is provided by the test) */
    int pid = k_spawn("MessagingTest00", MessagingEntryPoint, NULL, THREADS_MIN_STACK_SIZE, 5);       /* Staring Priority MIDDLE */

    if (pid < 0)
    {
        console_output(FALSE, "SchedulerEntryPoint: k_spawn failed (%d)\n", pid);
        k_exit(1);
    }

    /* Wait for the child to exit so shutdown is clean */
    int childExit = -999;
    int w = k_wait(&childExit);

    if (w < 0)
    {
        console_output(FALSE, "SchedulerEntryPoint: k_wait failed (%d)\n", w);
        k_exit(1);
    }

    k_exit(0);

    return 0;
} /* SchedulerEntryPoint */

/* ------------------------------------------------------------------------
   Name - mailbox_create
   Purpose - gets a free mailbox from the table of mailboxes and initializes it
   Parameters - maximum number of slots in the mailbox and the max size of a msg
                sent to the mailbox.
   Returns - -1 to indicate that no mailbox was created, or a value >= 0 as the
             mailbox id.
   ----------------------------------------------------------------------- */
int mailbox_create(int slots, int slot_size)
{
    int newId = -1;

    if (slots < 0 || slots > MAXSLOTS) return -1;
    if (slot_size <= 0 || slot_size > MAX_MESSAGE) return -1;

    disableInterrupts();

    for (int i = 0; i < MAXMBOX; i++)
    {
        // Reinit empty slots
        if (mailboxes[i].status == MBSTATUS_EMPTY)
        {
            mailboxes[i].pSlotListHead = NULL;
            mailboxes[i].mbox_id = i;
            mailboxes[i].slotSize = slot_size;
            mailboxes[i].slotCount = 0;             /* default 0 current messages in queue */

            if (slots == 0) mailboxes[i].type = MB_ZEROSLOT;
            else if (slots == 1) mailboxes[i].type = MB_SINGLESLOT;
            else mailboxes[i].type = MB_MULTISLOT;

            mailboxes[i].status = MBSTATUS_INUSE;

            /* IMPORTANT: you also need the capacity somewhere.
             * Your struct doesn't have "maxSlots", so many projects interpret
             * "slotCount" as current count and store max elsewhere.
             *
             * For now, we'll treat slotCount as CURRENT and store capacity in slotSize? no.
             * Better: add a new field to mailbox in message.h (allowed by "other items as needed")
             * BUT since you already have the struct, we can store capacity by reusing
             * slotCount as MAX until later? That breaks receive logic.
             *
             * So: we'll add a static array for maxSlots keyed by mailbox id.
             */
            newId = i;
            g_mailbox_maxSlots[i] = slots;     // TEST03 ADD
            break;
        }
    }

    enableInterrupts();
    return newId;
}

/* ------------------------------------------------------------------------
   Name - mailbox_send
   Purpose - Put a message into a slot for the indicated mailbox.
             Block the sending process if no slot available.
   Parameters - mailbox id, pointer to data of msg, # of bytes in msg,
                block flag.
   Returns - zero if successful, -1 if invalid args, -2 if would block
             (non-blocking mode), -5 if signaled while waiting.
   Side Effects - none.
   ----------------------------------------------------------------------- */
int mailbox_send(int mboxId, void* pMsg, int msg_size, int wait)
{
    checkKernelMode("mailbox_send");

    if (mboxId < 0 || mboxId >= MAXMBOX) return -1;
    if (msg_size < 0) return -1;                        // TEST 11 ALTER Remove reject NULL, 0 -Byte messages; allow them as valid
    if (msg_size > 0 && pMsg == NULL) return -1;

    disableInterrupts();

    MailBox* m = &mailboxes[mboxId];
    if (m->status != MBSTATUS_INUSE) {
        enableInterrupts();
        return -1;
    }
    if (msg_size > m->slotSize || msg_size > MAX_MESSAGE) {
        enableInterrupts();
        return -1;
    }

    /* First priority: if a receiver is already waiting, deliver directly */
    WaitingProcessPtr rnode = waitq_pop(&g_waitRecvHead[mboxId], &g_waitRecvTail[mboxId]);
    if (rnode != NULL)
    {
        int rpid = rnode->pid;
        MsgProcEntry* _msgProc = mp_for_pid(rpid);

        if (_msgProc && _msgProc->recvMax >= msg_size)                  // TEST 11 ALTER deliver only if receiver buffer is large enough
        {
            if (msg_size > 0)
                memcpy(_msgProc->recvBuf, pMsg, (size_t)msg_size);
            _msgProc->recvResult = msg_size;
        }
        else if (_msgProc) {
            _msgProc->recvResult = -1;
        }

        unblock(rpid);
        enableInterrupts();
        return 0;
    }

    /* Zero-slot mailbox: no buffering allowed */
    if (g_mailbox_maxSlots[mboxId] == 0)
    {
        if (!wait)
        {
            enableInterrupts();
            return -2;
        }

        /* Blocking sender waits for a receiver to arrive */
        int pid = k_getpid();
        MsgProcEntry* me = mp_for_pid(pid);
        WaitingProcessPtr snode = wp_for_pid(pid);

        if (!me || !snode)
        {
            enableInterrupts();
            return -1;
        }

        me->sendBuf = pMsg;
        me->sendSize = msg_size;
        me->sendResult = -9999;
        me->blockedMbox = mboxId;
        me->blockedType = BLOCKED_SEND;

        snode->pid = pid;
        snode->pNextProcess = NULL;
        snode->pPrevProcess = NULL;

        waitq_push(&g_waitSendHead[mboxId], &g_waitSendTail[mboxId], snode);

        block(BLOCKED_SEND);

        disableInterrupts();

        if (signaled())
        {
            me->blockedMbox = -1;
            me->blockedType = 0;

            enableInterrupts();
            return -5;
        }

        if (m->status != MBSTATUS_INUSE)
        {
            me->blockedMbox = -1;
            me->blockedType = 0;

            enableInterrupts();
            return -1;
        }

        {
            int sr = me->sendResult;

            me->blockedMbox = -1;
            me->blockedType = 0;

            enableInterrupts();
            return (sr == -9999) ? 0 : sr;
        }
    }

    /* Slotted mailbox path */
    if (m->slotCount >= g_mailbox_maxSlots[mboxId])
    {
        if (!wait)
        {
            enableInterrupts();
            return -2;
        }

        int pid = k_getpid();
        MsgProcEntry* me = mp_for_pid(pid);
        WaitingProcessPtr snode = wp_for_pid(pid);

        if (!me || !snode)
        {
            enableInterrupts();
            return -1;
        }

        me->sendBuf = pMsg;
        me->sendSize = msg_size;
        me->sendResult = -9999;
        me->blockedMbox = mboxId;
        me->blockedType = BLOCKED_SEND;

        snode->pid = pid;
        snode->pNextProcess = NULL;
        snode->pPrevProcess = NULL;

        waitq_push(&g_waitSendHead[mboxId], &g_waitSendTail[mboxId], snode);

        block(BLOCKED_SEND);

        disableInterrupts();

        if (signaled())
        {
            me->blockedMbox = -1;
            me->blockedType = 0;

            enableInterrupts();
            return -5;
        }

        if (m->status != MBSTATUS_INUSE)
        {
            me->blockedMbox = -1;
            me->blockedType = 0;

            enableInterrupts();
            return -1;
        }

        {
            int sr = me->sendResult;

            me->blockedMbox = -1;
            me->blockedType = 0;

            enableInterrupts();
            return (sr == -9999) ? 0 : sr;
        }
    }

    /* Space available in slotted mailbox: queue message */
    {
        SlotPtr s = allocate_slot();                            // TEST 16 ALTER Allocate slot here instead of before block to avoid holding up a slot while blocked if mailbox is full. (Also avoids unnecessary allocation if non-blocking.)
        if (!s) {
            enableInterrupts();
            console_output(FALSE, "No mail slots available.\n");
            stop(1);
        }

        s->mbox_id = mboxId;
        s->messageSize = msg_size;
        if (msg_size > 0)
        {
            memcpy(s->message, pMsg, (size_t)msg_size);             // TEST 11 ALTER Conditional copy to avoid invalid memcpy if msg_size is 0 (null pointer not allowed even if size is 0)
        }

        slot_enqueue(mboxId, s);
        m->slotCount++;

        enableInterrupts();
        return 0;
    }
}

/* ------------------------------------------------------------------------
   Name - mailbox_receive
   Purpose - Receive a message from the indicated mailbox.
             Block the receiving process if no message available.
   Parameters - mailbox id, pointer to buffer for msg, max size of buffer,
                block flag.
   Returns - size of received msg (>=0) if successful, -1 if invalid args,
             -2 if would block (non-blocking mode), -5 if signaled.
   Side Effects - none.
   ----------------------------------------------------------------------- */
int mailbox_receive(int mboxId, void* pMsg, int msg_size, int wait)
{
    checkKernelMode("mailbox_receive");

    if (mboxId < 0 || mboxId >= MAXMBOX) return -1;
    if (msg_size < 0) return -1;
    if (msg_size > 0 && pMsg == NULL) return -1;

    disableInterrupts();

    MailBox* m = &mailboxes[mboxId];
    if (m->status != MBSTATUS_INUSE) {
        enableInterrupts();
        return -1;
    }

    /* First try queued mail (slotted mailbox path) */
    SlotPtr s = slot_dequeue(mboxId);
    if (s)
    {
        if (msg_size < s->messageSize)
        {
            /* Put back at head */
            s->pNextSlot = m->pSlotListHead;
            s->pPrevSlot = NULL;

            if (m->pSlotListHead)
                m->pSlotListHead->pPrevSlot = s;
            else
                g_slotTail[mboxId] = s;

            m->pSlotListHead = s;

            enableInterrupts();
            return -1;
        }

        {
            int n = s->messageSize;
            if (n > 0)
            {
                memcpy(pMsg, s->message, (size_t)n);        // TEST 11 ALTER Conditional copy to avoid invalid memcpy if msg_size is 0 (null pointer not allowed even if size is 0)
            }
            m->slotCount--;
            free_slot(s);

            /* For slotted mailboxes only: if a sender was blocked because mailbox was full,
               one slot just opened up, so queue one sender's pending message now. */
            if (g_mailbox_maxSlots[mboxId] > 0)
            {
                WaitingProcessPtr snode = waitq_pop(&g_waitSendHead[mboxId], &g_waitSendTail[mboxId]);
                if (snode)
                {
                    int spid = snode->pid;
                    MsgProcEntry* se = mp_for_pid(spid);

                    if (se &&
                        m->status == MBSTATUS_INUSE &&
                        m->slotCount < g_mailbox_maxSlots[mboxId])
                    {
                        SlotPtr ns = allocate_slot();
                        if (ns != NULL)
                        {
                            ns->mbox_id = mboxId;
                            ns->messageSize = se->sendSize;
                            memcpy(ns->message, se->sendBuf, (size_t)se->sendSize);

                            slot_enqueue(mboxId, ns);
                            m->slotCount++;
                            se->sendResult = 0;
                        }
                        else
                        {
                            enableInterrupts();
                            console_output(FALSE, "No mail slots available.\n");            // TEST 16 ALTER If we fail to allocate a slot for the sender, we have to unblock them with an error instead of leaving them blocked forever. (Also avoids unnecessary allocation if non-blocking.)
                            stop(1);
                        }
                    }
                    else if (se)
                    {
                        se->sendResult = -1;
                    }

                    unblock(spid);
                }
            }

            enableInterrupts();
            return n;
        }
    }

    /* Zero-slot mailbox: if a sender is already waiting, take directly from sender */
	// TEST29 ALTER zero-slot path before blocking to preserve rendezvous behavior for zero-slot mailboxes (instead of blocking sender and waiting for receiver to arrive, which would deadlock since receiver is what we're trying to unblock in the first place).
    if (g_mailbox_maxSlots[mboxId] == 0)
    {
        WaitingProcessPtr snode = waitq_pop(&g_waitSendHead[mboxId], &g_waitSendTail[mboxId]);
        if (snode)
        {
            int spid = snode->pid;
            MsgProcEntry* se = mp_for_pid(spid);

            if (!se || se->sendSize < 0 || msg_size < se->sendSize)
            {
                if (se)
                    se->sendResult = -1;

                unblock(spid);
                enableInterrupts();
                return -1;
            }

            if (se->sendSize > 0)
            {
                memcpy(pMsg, se->sendBuf, (size_t)se->sendSize);
            }

            se->sendResult = 0;
            unblock(spid);

            enableInterrupts();
            return se->sendSize;
        }
    }

    if (!wait)
    {
        enableInterrupts();
        return -2;
    }

    /* Block waiting receiver */
    {
        int pid = k_getpid();
        MsgProcEntry* me = mp_for_pid(pid);
        WaitingProcessPtr node = wp_for_pid(pid);

        if (!me || !node)
        {
            enableInterrupts();
            return -1;
        }

        me->recvBuf = pMsg;
        me->recvMax = msg_size;
        me->recvResult = -9999;
        me->blockedMbox = mboxId;
        me->blockedType = BLOCKED_RECEIVE;

        node->pid = pid;
        node->pNextProcess = NULL;
        node->pPrevProcess = NULL;

        waitq_push(&g_waitRecvHead[mboxId], &g_waitRecvTail[mboxId], node);

        block(BLOCKED_RECEIVE);

        disableInterrupts();

        if (signaled())
        {
            me->blockedMbox = -1;
            me->blockedType = 0;

            enableInterrupts();
            return -5;
        }

        if (m->status != MBSTATUS_INUSE)
        {
            me->blockedMbox = -1;
            me->blockedType = 0;

            enableInterrupts();
            return -1;
        }

        {
            int result = me->recvResult;

            me->blockedMbox = -1;
            me->blockedType = 0;

            enableInterrupts();
            return result;
        }
    }
}

/* ------------------------------------------------------------------------
   Name - mailbox_free
   Purpose - Frees a previously created mailbox. Any process waiting on
             the mailbox should be signaled and unblocked.
   Parameters - mailbox id.
   Returns - zero if successful, -1 if invalid args, -5 if signaled
             while closing the mailbox.
   ----------------------------------------------------------------------- */
int mailbox_free(int mboxId)
{
    checkKernelMode("mailbox_free");

    if (mboxId < 0 || mboxId >= MAXMBOX)
        return -1;

    disableInterrupts();

    MailBox* m = &mailboxes[mboxId];
    if (m->status != MBSTATUS_INUSE)
    {
        enableInterrupts();
        return -1;
    }

    /* Mark released first so blocked send/recv paths detect closure */
    m->status = MBSTATUS_RELEASED;

    /* Free queued slots */
    {
        SlotPtr s = m->pSlotListHead;
        while (s != NULL)
        {
            SlotPtr next = s->pNextSlot;
            free_slot(s);
            s = next;
        }
    }

    m->pSlotListHead = NULL;
    g_slotTail[mboxId] = NULL;
    m->slotCount = 0;

    /* Wake all blocked receivers and senders */
    {
        WaitingProcessPtr node;

        while ((node = waitq_pop(&g_waitRecvHead[mboxId], &g_waitRecvTail[mboxId])) != NULL)
        {
            int pid = node->pid;
            MsgProcEntry* me = mp_for_pid(pid);

            if (me)
            {
                me->blockedMbox = mboxId;
                me->blockedType = BLOCKED_RECEIVE;
            }

            k_kill(pid, SIG_TERM);
            unblock(pid);
        }

        while ((node = waitq_pop(&g_waitSendHead[mboxId], &g_waitSendTail[mboxId])) != NULL)
        {
            int pid = node->pid;
            MsgProcEntry* me = mp_for_pid(pid);

            if (me)
            {
                me->blockedMbox = mboxId;
                me->blockedType = BLOCKED_SEND;
            }

            k_kill(pid, SIG_TERM);
            unblock(pid);
        }
    }

    /* Reset mailbox state so it can be reused by mailbox_create() */
    m->pSlotListHead = NULL;
    g_slotTail[mboxId] = NULL;
    m->mbox_id = mboxId;
    m->slotSize = 0;
    m->slotCount = 0;
    m->type = MB_MAXTYPES;
    m->status = MBSTATUS_EMPTY;

    g_mailbox_maxSlots[mboxId] = 0;
    g_waitRecvHead[mboxId] = NULL;
    g_waitRecvTail[mboxId] = NULL;
    g_waitSendHead[mboxId] = NULL;
    g_waitSendTail[mboxId] = NULL;
    g_releaseWaitCount[mboxId] = 0;
    g_releaseFreerPid[mboxId] = -1;

    enableInterrupts();

    if (signaled())
        return -5;

    return 0;
}

/* ------------------------------------------------------------------------
   Name - wait_device
   Purpose - Waits for a device interrupt by blocking on the device's
             mailbox. Returns the device status via the status pointer.
   Parameters - device name string, pointer to status output.
   Returns - 0 if successful, -1 if invalid parameter, -5 if signaled.
   ----------------------------------------------------------------------- */
int wait_device(char* deviceName, int* status)
{
    int result = 0;
    int deviceHandle = -1;              // Use int for consistency with device APIs
    checkKernelMode("waitdevice");

    /* Basic parameter validation */
    if (deviceName == NULL || status == NULL)
    {
        return -1;
    }

    /* Allow interrupts while waiting for device interrupt */
    enableInterrupts();

    if (strcmp(deviceName, "clock") == 0)
    {
        deviceHandle = THREADS_CLOCK_DEVICE_ID;
    }
    else
    {
        deviceHandle = device_handle(deviceName);

        /* TEST05 ADD:
         * Ensure the device was properly initialized and handle is valid.
         */
        if (deviceHandle < 0 || deviceHandle >= THREADS_MAX_DEVICES)
        {
            console_output(FALSE, "wait_device: Unknown or uninitialized device %s.\n", deviceName);
            stop(-1);
        }
    }

    if (deviceHandle >= 0 && deviceHandle < THREADS_MAX_DEVICES)
    {
        /* Make sure this device has a mailbox */
        if (devices[deviceHandle].deviceMbox < 0)
        {
            console_output(FALSE, "wait_device: No mailbox for device %s (handle %d).\n", deviceName, deviceHandle);
            stop(-1);
        }

        /* set a flag that there is a process waiting on a device. */
        waitingOnDevice++;

        /* TEST05 ADD - Adding check for possible failure */
        int mail_status = mailbox_receive(
            devices[deviceHandle].deviceMbox,
            status,
            sizeof(int),
            TRUE /* blocking */
        );

        if (mail_status < 0)
        {
            result = mail_status;   // Propagate mailbox failure
        }

        /* Re-disable interrupts after returning from block */
        disableInterrupts();
        waitingOnDevice--;
    }
    else
    {
        console_output(FALSE, "Unknown device type.");
        stop(-1);
    }

    /* If process was signaled while waiting, return -5 */
    if (signaled())
    {
        result = -5;    // Signaled return -5
    }

    return result;
}

int check_io_messaging(void)
{
    if (waitingOnDevice)
    {
        return 1;
    }
    return 0;
}

static void InitializeHandlers()
{
    handlers = get_interrupt_handlers();

    // Also initialize the system call vector(systemCallVector).* /
    // Using syscall with nullsys by default                    // TEST05 ADD
    for (int i = 0; i < THREADS_MAX_SYSCALLS; i++)
    {
        systemCallVector[i] = nullsys;
    }

    /* TODO: Register interrupt handlers in the handlers array.
     * Use the interrupt indices defined in THREADSLib.h:
     *   handlers[THREADS_TIMER_INTERRUPT]   = your_clock_handler;
     *   handlers[THREADS_IO_INTERRUPT]      = your_io_handler;
     *   handlers[THREADS_SYS_CALL_INTERRUPT] = your_syscall_handler;*/

    handlers[THREADS_TIMER_INTERRUPT] = clock_handler_messaging;            // TEST09 ADD (per lecture) new clock handler
    handlers[THREADS_IO_INTERRUPT] = io_handler;                            // TEST05 ADD
    handlers[THREADS_SYS_CALL_INTERRUPT] = syscall_handler;                 // TEST05 ADD
}

/* an error method to handle invalid syscalls */
static void nullsys(system_call_arguments_t* args)
{
    console_output(FALSE, "nullsys(): Invalid syscall %d. Halting...\n", args->call_id);
    stop(1);
} /* nullsys */

static void init_slot_freelist(void)
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
static SlotPtr allocate_slot(void)
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
static void free_slot(SlotPtr _slotptr)
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
static void init_mailboxes(void)
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

        g_releaseWaitCount[i] = 0;                  // TEST09 ADD
        g_releaseFreerPid[i] = -1;                  // TEST09 ADD
    }

}

static void waitq_push(WaitingProcessPtr* head, WaitingProcessPtr* tail, WaitingProcessPtr n) {
    n->pNextProcess = NULL;
    n->pPrevProcess = *tail;
    if (*tail) (*tail)->pNextProcess = n;
    else *head = n;
    *tail = n;
}

static WaitingProcessPtr waitq_pop(WaitingProcessPtr* head, WaitingProcessPtr* tail) {
    WaitingProcessPtr n = *head;
    if (!n) return NULL;
    *head = n->pNextProcess;
    if (*head) (*head)->pPrevProcess = NULL;
    else *tail = NULL;
    n->pNextProcess = n->pPrevProcess = NULL;
    return n;
}

static void slot_enqueue(int mboxId, SlotPtr s) {
    MailBox* m = &mailboxes[mboxId];
    s->pNextSlot = NULL;
    s->pPrevSlot = g_slotTail[mboxId];
    if (g_slotTail[mboxId]) g_slotTail[mboxId]->pNextSlot = s;
    else m->pSlotListHead = s;
    g_slotTail[mboxId] = s;
}

static SlotPtr slot_dequeue(int mboxId) {
    MailBox* m = &mailboxes[mboxId];
    SlotPtr s = m->pSlotListHead;
    if (!s) return NULL;
    m->pSlotListHead = s->pNextSlot;
    if (m->pSlotListHead) m->pSlotListHead->pPrevSlot = NULL;
    else g_slotTail[mboxId] = NULL;
    s->pNextSlot = s->pPrevSlot = NULL;
    return s;
}

static void io_handler(char deviceId[32], uint8_t command, uint32_t status, void* pArgs)
{
    (void)command;
    (void)pArgs;

    /* TEST05 ADD - Convert deviceId parameter into device index safely */
    int idx = device_id_from_param(deviceId);

    if (idx < 0 || idx >= THREADS_MAX_DEVICES)
    {
        return;
    }

    // TEST08 ADD If this device wasn't initialized, ignore the interrupt safely
    if (devices[idx].deviceMbox < 0)
        return;

    int st = (int)status;

    /* Interrupt context: must be non-blocking */
    mailbox_send(devices[idx].deviceMbox, &st, sizeof(int), FALSE);
}

// TEST09 ADD - New clock handler for Messaging project (per lecture)
// NOTE Do NOT name this clock_handler (symbol already exists in Scheduler project)
static void clock_handler_messaging(char deviceId[32], uint8_t command, uint32_t status, void* pArgs)
{
    (void)command;
    (void)status;
    (void)pArgs;

    /* In this THREADS build, deviceId is effectively a numeric device index */
    int idx = (int)(uintptr_t)deviceId;

    /* Validate index */
    if (idx < 0 || idx >= THREADS_MAX_DEVICES)
    {
        return;
    }

    /* Must still time slice for round-robin scheduling */
    time_slice();

    // TEST08 ADD Every 5th clock interrupt, send a tick to the clock mailbox (non-blocking) */
    static int tickCount = 0;
    tickCount++;

    if ((tickCount % 5) == 0)
    {
        /* Clock mailbox should be at devices[THREADS_CLOCK_DEVICE_ID] */
        int clockIdx = THREADS_CLOCK_DEVICE_ID;

        /* If the clock mailbox isn't initialized, just skip safely */
        if (devices[clockIdx].deviceMbox < 0)
            return;

        /* "Tick" message content doesn't really matter; presence matters */
        int tick = tickCount;

        /* Interrupt context: must be non-blocking */
        mailbox_send(devices[clockIdx].deviceMbox, &tick, sizeof(int), FALSE);
    }
}

static void syscall_handler(char deviceId[32], uint8_t command, uint32_t status, void* pArgs)
{
    (void)deviceId; (void)command; (void)status;
    system_call_arguments_t* args = (system_call_arguments_t*)pArgs;
    if (args->call_id < 0 || args->call_id >= THREADS_MAX_SYSCALLS) {
        nullsys(args);
        return;
    }
    systemCallVector[args->call_id](args);
}

// TEST05 ADD - sure up device io_handler...Search by name
static int find_device_index_by_name(const char* name)
{
    for (int i = 0; i < THREADS_MAX_DEVICES; i++) {
        if (devices[i].deviceName[0] != '\0' && strcmp(devices[i].deviceName, name) == 0)
            return i;
    }
    return -1;
}

// Handle pointer values as numeric     // TEST05 ADD - Fix exception in io_handler
static inline int device_id_from_param(char deviceId[32])
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

// TEST09 ADD DO NOT use pid % MAXPROC (pid collisions kill the wrong process)
static int mpIndex(int pid)
{
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

/* TEST05 ADD: proc table helpers */
/* TEST09 FIX proc table helpers */
static MsgProcEntry* mp_for_pid(int pid)
{
    int idx = mpIndex(pid);
    if (idx < 0) return NULL;
    return &g_msgProc[idx];
}

static MsgProcEntry* mp_self(void)
{
    return mp_for_pid(k_getpid());
}

static WaitingProcessPtr wp_for_pid(int pid)
{
    int idx = mpIndex(pid);
    if (idx < 0) return NULL;

    g_waitNode[idx].pid = pid;
    g_waitNode[idx].pNextProcess = NULL;
    g_waitNode[idx].pPrevProcess = NULL;
    return &g_waitNode[idx];
}


/*****************************************************************************
   Name - checkKernelMode
   Purpose - Checks the PSR for kernel mode and halts if in user mode
   Parameters -
   Returns -
****************************************************************************/
static inline void checkKernelMode(const char* functionName)
{
    union psr_values psrValue;

    psrValue.integer_part = get_psr();
    if (psrValue.bits.cur_mode == 0)
    {
        console_output(FALSE, "Kernel mode expected, but function called in user mode.\n");
        stop(1);
    }
}