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
/////////////////////////////////////////////////////////////////////////

/* ------------------------------------------------------------------------
     Name - SchedulerEntryPoint
     Purpose - Initializes mailboxes and interrupt vector.
               Start the Messaging test process.
     Parameters - one, default arg passed by k_spawn that is not used here.
----------------------------------------------------------------------- */
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
    init_slot_freelist();       // TEST03 ADD

    ///////////////////////////////////////////////////////////////////////////////////////////////
    /* TODO: Initialize the devices using device_initialize().
     * The devices are: disk0, disk1, term0, term1, term2, term3.
     * Store the device handle and name in the devices array.
     */

     // Create device mailboxes
     // TODO: Create mailboxes for each device.
     //   devices[THREADS_CLOCK_DEVICE_ID].deviceMbox = mailbox_create(0, sizeof(int));      // TEST05 ADD
    devices[THREADS_CLOCK_DEVICE_ID].deviceMbox = mailbox_create(0, sizeof(int)); /* clock: zero-slot */

    // IO devices: slotted mailbox; handler uses non-blocking sends
    //   devices[i].deviceMbox = mailbox_create(..., sizeof(int));            // TEST05 ADD

    // IMPORTANT (TEST05 FIX):
    // device_handle("term0") may be invalid until AFTER device_initialize("term0").
    // So initialize first, then use the returned handle/index to assign deviceMbox/deviceName.
    int term0Handle = device_initialize("term0");                                // TEST05 ADD
    if (term0Handle < 0 || term0Handle >= THREADS_MAX_DEVICES)                  // TEST05 ADD
    {
        console_output(FALSE, "SchedulerEntryPoint: device_initialize(term0) failed (%d)\n", term0Handle);
        stop(1);
    }

    devices[term0Handle].deviceHandle = (void*)(uintptr_t)term0Handle;          // TEST05 ADD
    devices[term0Handle].deviceMbox = mailbox_create(10, sizeof(int));          // TEST05 ADD
    strncpy(devices[term0Handle].deviceName, "term0", sizeof(devices[0].deviceName) - 1);

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
    if (pMsg == NULL) return -1;
    if (msg_size < 0) return -1;

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

    /* If a receiver is waiting, deliver directly to its buffer and unblock it */
    WaitingProcessPtr rnode = waitq_pop(&g_waitRecvHead[mboxId], &g_waitRecvTail[mboxId]);
    if (rnode) {
        int rpid = rnode->pid;
        int ridx = mpIndex(rpid);

        if (g_msgProc[ridx].pid == rpid && g_msgProc[ridx].recvBuf && g_msgProc[ridx].recvMax >= msg_size) {
            memcpy(g_msgProc[ridx].recvBuf, pMsg, (size_t)msg_size);
            g_msgProc[ridx].recvResult = msg_size;
        }
        else {
            /* Receiver buffer invalid/too small: fail it */
            g_msgProc[ridx].recvResult = -1;
        }

        /* Unblock receiver AFTER data is in place */
        unblock(rpid);

        enableInterrupts();
        return 0;
    }

    /* No waiting receiver: normal slotted mailbox enqueue */
    if (g_mailbox_maxSlots[mboxId] > 0 && m->slotCount >= g_mailbox_maxSlots[mboxId]) {
        if (!wait) {
            enableInterrupts();
            return -2;
        }

        /* Blocking sender: enqueue sender and sleep */
        int pid = k_getpid();
        int idx = mpIndex(pid);

        g_msgProc[idx].pid = pid;
        g_msgProc[idx].sendBuf = pMsg;
        g_msgProc[idx].sendSize = msg_size;
        g_msgProc[idx].sendResult = -9999;

        WaitingProcessPtr snode = &g_waitNode[idx];
        snode->pid = pid;
        snode->pNextProcess = snode->pPrevProcess = NULL;
        waitq_push(&g_waitSendHead[mboxId], &g_waitSendTail[mboxId], snode);

        block(BLOCKED_SEND);

        /* When we return, interrupts may be enabled */
        disableInterrupts();

        if (m->status != MBSTATUS_INUSE) { enableInterrupts(); return -1; }
        if (signaled()) { enableInterrupts(); return -5; }

        /* Receiver should have delivered our message and set sendResult */
        int sr = g_msgProc[idx].sendResult;
        enableInterrupts();
        return (sr == -9999) ? 0 : sr;   /* default success if not set */
    }

    SlotPtr s = allocate_slot();
    if (!s) {
        enableInterrupts();
        return -1;
    }

    s->mbox_id = mboxId;
    s->messageSize = msg_size;
    memcpy(s->message, pMsg, (size_t)msg_size);

    slot_enqueue(mboxId, s);
    m->slotCount++;

    enableInterrupts();
    return 0;
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
    if (pMsg == NULL) return -1;
    if (msg_size < 0) return -1;

    disableInterrupts();

    MailBox* m = &mailboxes[mboxId];
    if (m->status != MBSTATUS_INUSE) {
        enableInterrupts();
        return -1;
    }

    /* If a message is already queued, take it FIFO */
    SlotPtr s = slot_dequeue(mboxId);
    if (s) {
        if (msg_size < s->messageSize) {
            /* Put it back at head (simple) */
            s->pNextSlot = m->pSlotListHead;
            if (m->pSlotListHead) m->pSlotListHead->pPrevSlot = s;
            m->pSlotListHead = s;
            if (!g_slotTail[mboxId]) g_slotTail[mboxId] = s;

            enableInterrupts();
            return -1;
        }

        int n = s->messageSize;
        memcpy(pMsg, s->message, (size_t)n);
        m->slotCount--;
        free_slot(s);

        // TEST06 ADD  If a sender is blocked because the mailbox was full, we just freed a slot.
        // Wake ONE blocked sender and deliver its pending message into the mailbox.
        WaitingProcessPtr snode = waitq_pop(&g_waitSendHead[mboxId], &g_waitSendTail[mboxId]);
        if (snode)
        {
            int spid = snode->pid;
            int sidx = mpIndex(spid);

            /* If the mailbox is still valid, place the sender's message into a new slot */
            if (m->status == MBSTATUS_INUSE &&
                (g_mailbox_maxSlots[mboxId] == 0 || m->slotCount < g_mailbox_maxSlots[mboxId]))
            {
                SlotPtr ns = allocate_slot();
                if (ns != NULL)
                {
                    int ssize = g_msgProc[sidx].sendSize;
                    void* sbuf = g_msgProc[sidx].sendBuf;

                    ns->mbox_id = mboxId;
                    ns->messageSize = ssize;
                    memcpy(ns->message, sbuf, (size_t)ssize);

                    /* Enqueue at tail (FIFO) */
                    slot_enqueue(mboxId, ns);
                    m->slotCount++;

                    g_msgProc[sidx].sendResult = 0;    /* sender's mailbox_send returns success */
                }
                else
                {
                    /* Should be rare (we just freed one), but be safe */
                    g_msgProc[sidx].sendResult = -1;
                }
            }
            else
            {
                g_msgProc[sidx].sendResult = -1;
            }

            /* Unblock the sender AFTER its message has been queued */
            unblock(spid);
        }

        enableInterrupts();
        return n;
    }

    /* No queued mail: if non-blocking, would-block */
    if (!wait) {
        enableInterrupts();
        return -2;
    }

    /* Blocking: put this process on the receive wait queue */
    int pid = k_getpid();
    int idx = mpIndex(pid);

    g_msgProc[idx].pid = pid;
    g_msgProc[idx].recvBuf = pMsg;
    g_msgProc[idx].recvMax = msg_size;
    g_msgProc[idx].recvResult = -9999;

    WaitingProcessPtr node = &g_waitNode[idx];
    node->pid = pid;
    node->pNextProcess = node->pPrevProcess = NULL;
    waitq_push(&g_waitRecvHead[mboxId], &g_waitRecvTail[mboxId], node);

    /* block() may re-enable interrupts internally */
    int br = block(BLOCKED_RECEIVE);

    /* When we return, re-enter with interrupts off for safety */
    disableInterrupts();

    /* If the mailbox got released while waiting, treat as invalid */
    if (m->status != MBSTATUS_INUSE) {
        enableInterrupts();
        return -1;
    }

    if (signaled()) {
        enableInterrupts();
        return -5;
    }

    int result = g_msgProc[idx].recvResult;
    enableInterrupts();
    return result;
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
    int result = -1;

    return result;
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
static void free_slot(SlotPtr _slotptr)
{
    if (!_slotptr) return;
    _slotptr->pNextSlot = freeSlotHead;
    _slotptr->pPrevSlot = NULL;
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

    int idx = device_id_from_param(deviceId);
    if (idx < 0 || idx >= THREADS_MAX_DEVICES)
    {
        return;
    }

    int st = (int)status;

    /* Interrupt context: must be non-blocking */
    mailbox_send(devices[idx].deviceMbox, &st, sizeof(int), FALSE);
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
    uintptr_t raw = (uintptr_t)deviceId;

    // Low pointers are invalid
    if (raw < 4096) {
        return (int)raw;
    }

    // Otherise use string name
    if (deviceId == NULL) return -1;
    if (strcmp(deviceId, "clock") == 0) return THREADS_CLOCK_DEVICE_ID;

    return device_handle((char*)deviceId);
}

static int mpIndex(int pid)
{
    return pid % MAXPROC;
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