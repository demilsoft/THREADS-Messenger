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
#include "Messaging.h"
#include <stdint.h>
#include "message.h"
#include "MessagingHelpers.h"

/////////////////////////////////////// GLOBALS ///////////////////////////////////////
interrupt_handler_t* handlers;                  // From THREADs, interrupt handler array of function pointers
void (*systemCallVector[THREADS_MAX_SYSCALLS])(system_call_arguments_t* args); // system call array of function pointers
MailBox mailboxes[MAXMBOX];                     // Mail boxes
MailSlot mailSlots[MAXSLOTS];                   // Mail slots

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

static DeviceManagementData devices[THREADS_MAX_DEVICES];
static int waitingOnDevice = 0;                 // Device wait process count
/////////////////////////////////////// GLOBALS ///////////////////////////////////////

////////////////////////////////////// PROTOTYPES /////////////////////////////////////
static void nullsys(system_call_arguments_t* args);
static void InitializeHandlers(void);
static int check_io_messaging(void);
extern int MessagingEntryPoint(void*);
static void checkKernelMode(const char* functionName);
static void init_devices(void);
static void io_handler(char deviceId[32], uint8_t command, uint32_t status, void* pArgs);               // TEST05 ADD
static void syscall_handler(char deviceId[32], uint8_t command, uint32_t status, void* pArgs);          // TEST05 ADD
static void clock_handler_messaging(char deviceId[32], uint8_t command, uint32_t status, void* pArgs);  // TEST08 ADD
////////////////////////////////////// PROTOTYPES /////////////////////////////////////

// SCHEDULER ENTRY POINT //
int SchedulerEntryPoint(void* arg)
{
    (void)arg;

    // TEST03 ADD check for kernel mode
    checkKernelMode("SchedulerEntryPoint");

    // Disable interrupts 
    disableInterrupts();

    // TEST25 MAXMBOX sanity check
    //console_output(FALSE, "DEBUG: MAXMBOX=%d\n", MAXMBOX);
    //console_output(FALSE, "DEBUG: mailboxes entries=%d\n", (int)(sizeof(mailboxes) / sizeof(mailboxes[0])));

    // set this to the real check_io function. 
    check_io = check_io_messaging;

    /* Initialize the mail box table, slots, & other data structures.
     * Initialize int_vec and sys_vec, allocate mailboxes for interrupt
     * handlers.  Etc... */

     /* Initialize the devices and their mailboxes. */
     /* Allocate mailboxes for use by the interrupt handlers.
      * Note: The clock device uses a zero-slot mailbox, while I/O devices
      * (disks, terminals) need slotted mailboxes since their interrupt
      * handlers use non-blocking sends. */

      // TEST03 ADD: init mailbox/slot structures first so mailbox_create works cleanly.
    init_mailboxes();                       // TEST03 ADD
    init_slot_freelist();                   // TEST03 ADD
    init_proc_table();                      // TEST05 ADD 
    init_devices();                         // TEST05 ADD 

    InitializeHandlers();
    enableInterrupts();

    // Spawn the test process 
    int pid = k_spawn("MessagingTest00", MessagingEntryPoint, NULL, THREADS_MIN_STACK_SIZE, 5);       /* Staring Priority MIDDLE */

    if (pid < 0)
    {
        console_output(FALSE, "SchedulerEntryPoint: k_spawn failed (%d)\n", pid);
        k_exit(1);
    }

    // Wait for the child to exit so shutdown is clean 
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

static void init_devices(void)
{
    // MAKE SURE TO INITIALIZE YOUR DEVICE MANAGEMENT DATA STRUCTURE
    for (int i = 0; i < THREADS_MAX_DEVICES; i++)
    {
        // Clear device management
        devices[i].deviceHandle = NULL;
        devices[i].deviceMbox = -1;
        devices[i].deviceType = 0;
        devices[i].deviceName[0] = '\0';        // NULL TERM
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////
    /* TODO: Initialize the devices using device_initialize().
     * The devices are: disk0, disk1, term0, term1, term2, term3.
     * Store the device handle and name in the devices array.
     */

     // Create device mailboxes
     // TODO: Create mailboxes for each device.
     // devices[THREADS_CLOCK_DEVICE_ID].deviceMbox = mailbox_create(0, sizeof(int));      // TEST05 ADD

     // NOTE (per lecture):
     // We create 1 clock mailbox + 2 disk mailboxes + 4 terminal mailboxes (7 total),
     // then create ONE extra mailbox so mailbox IDs align with instructor output.      // TEST09 ADD

     // TEST08 FIX CLOCK fixed device index 
    devices[THREADS_CLOCK_DEVICE_ID].deviceHandle = (void*)(uintptr_t)THREADS_CLOCK_DEVICE_ID;
    strncpy(devices[THREADS_CLOCK_DEVICE_ID].deviceName, "clock", sizeof(devices[0].deviceName) - 1);
    devices[THREADS_CLOCK_DEVICE_ID].deviceName[sizeof(devices[0].deviceName) - 1] = '\0';

    // TEST08 FIX Clock device uses a zero-slot mailbox 
    devices[THREADS_CLOCK_DEVICE_ID].deviceMbox = mailbox_create(0, sizeof(int));
    if (devices[THREADS_CLOCK_DEVICE_ID].deviceMbox < 0)
    {
        console_output(FALSE, "SchedulerEntryPoint: mailbox_create(clock) failed\n");
        stop(1);
    }

    // TEST07 ALTER TO LOOP OVER ALL DEVICES AND INITIALIZE
    // TEST08 FIX I/O devices that may interrupt 
    const char* ioDevices[] = { "disk0", "disk1", "term0", "term1", "term2", "term3" };

    for (int i = 0; i < 6; i++)
    {
        // Initialize one I/O device
        int _Device = device_initialize((char*)ioDevices[i]);
        if (_Device < 0 || _Device >= THREADS_MAX_DEVICES)
        {
            console_output(FALSE, "SchedulerEntryPoint: device_initialize(%s) failed (%d)\n", ioDevices[i], _Device);
            stop(1);
        }

        // Store by HANDLER INDEX
        devices[_Device].deviceHandle = (void*)(uintptr_t)_Device;
        strncpy(devices[_Device].deviceName, ioDevices[i], sizeof(devices[0].deviceName) - 1);
        devices[_Device].deviceName[sizeof(devices[_Device].deviceName) - 1] = '\0';

        // I/O devices need slotted mailbox because interrupt handler uses non-blocking send *
        devices[_Device].deviceMbox = mailbox_create(10, sizeof(int));
        if (devices[_Device].deviceMbox < 0)
        {
            console_output(FALSE, "SchedulerEntryPoint: mailbox_create(%s) failed\n", ioDevices[i]);
            stop(1);
        }
    }

    // TEST09 ADD adding one to accommodate extra add by Prof Duren
    (void)mailbox_create(1, sizeof(int));
    ///////////////////////////////////////////////////////////////////////////////////////////////
}

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

    if (slots < 0 || slots > MAXSLOTS)
    {
        return -1;
    }
    if (slot_size <= 0 || slot_size > MAX_MESSAGE)
    {
        return -1;
    }

    disableInterrupts();

    for (int i = 0; i < MAXMBOX; i++)
    {
        // Reinit empty slots
        if (mailboxes[i].status == MBSTATUS_EMPTY)
        {
            // Init mailbox
            mailboxes[i].pSlotListHead = NULL;
            mailboxes[i].mbox_id = i;
            mailboxes[i].slotSize = slot_size;
            mailboxes[i].slotCount = 0;                     // default 0 current messages in queue 

            if (slots == 0)
            {
                mailboxes[i].type = MB_ZEROSLOT;
            }
            else if (slots == 1)
            {
                mailboxes[i].type = MB_SINGLESLOT;
            }
            else
            {
                mailboxes[i].type = MB_MULTISLOT;
            }

            // Mark mailbox as allocated
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
             * So: we'll add a static array for maxSlots keyed by mailbox id.*/
            newId = i;
            g_mailbox_maxSlots[i] = slots;                  // TEST03 ADD
            g_slotTail[i] = NULL;                           // TEST05 ADD keep tail reset on create
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

    if (mboxId < 0 || mboxId >= MAXMBOX)
    {
        return -1;
    }
    if (msg_size < 0)
    {
        return -1;                               // TEST11 ALTER Remove reject NULL
    }
    if (msg_size > 0 && pMsg == NULL)
    {
        return -1;
    }

    disableInterrupts();

    MailBox* _Mailbox = &mailboxes[mboxId];
    if (_Mailbox->status != MBSTATUS_INUSE)
    {
        enableInterrupts();
        return -1;
    }
    if (msg_size > _Mailbox->slotSize || msg_size > MAX_MESSAGE)
    {
        enableInterrupts();
        return -1;
    }

    // If a receiver is already waiting, deliver directly 
    WaitingProcessPtr rnode = waitq_pop(&g_waitRecvHead[mboxId], &g_waitRecvTail[mboxId]);
    if (rnode != NULL)
    {
        int rpid = rnode->pid;
        MsgProcEntry* _msgProc = mp_for_pid(rpid);

        if (_msgProc && _msgProc->recvMax >= msg_size)             // TEST11 ALTER deliver if receiver buffer is large enough
        {
            if (msg_size > 0)
            {
                memcpy(_msgProc->recvBuf, pMsg, (size_t)msg_size);
            }
            _msgProc->recvResult = msg_size;
        }
        else if (_msgProc)
        {
            _msgProc->recvResult = -1;
        }

        unblock(rpid);
        enableInterrupts();
        return 0;
    }

    // 0 slot mailbox: no buffering allowed 
    if (g_mailbox_maxSlots[mboxId] == 0)
    {
        if (!wait)
        {
            enableInterrupts();
            return -2;
        }

        // Blocking sender waits for a receiver to arrive 
        int pid = k_getpid();
        MsgProcEntry* _msgProcEntry = mp_for_pid(pid);
        WaitingProcessPtr snode = wp_for_pid(pid);

        if (!_msgProcEntry || !snode)
        {
            enableInterrupts();
            return -1;
        }

        prepare_blocked_sender(_msgProcEntry, mboxId, pMsg, msg_size);        // TEST25? ADD CLEANUP

        // Initialize sender wait node
        snode->pid = pid;
        snode->pNextProcess = NULL;
        snode->pPrevProcess = NULL;

        waitq_push(&g_waitSendHead[mboxId], &g_waitSendTail[mboxId], snode);

        block(BLOCKED_SEND);

        disableInterrupts();

        // Mailbox release path 
        if (_msgProcEntry->blockedMbox == mboxId && mailboxes[mboxId].status == MBSTATUS_RELEASED)
        {
            // One released waiter finished
            g_releaseWaitCount[mboxId]--;
            if (g_releaseWaitCount[mboxId] == 0 && g_releaseFreerPid[mboxId] >= 0)
            {
                // Wake freeing process last
                unblock(g_releaseFreerPid[mboxId]);
                g_releaseFreerPid[mboxId] = -1;
            }

            return finish_blocked_call(_msgProcEntry, -5);                 // TEST25 ADD CLEANUP
        }

        // Main signal path 
        if (signaled())
        {
            return finish_blocked_call(_msgProcEntry, -5);                 // TEST25 ADD CLEANUP
        }

        if (_Mailbox->status != MBSTATUS_INUSE)
        {
            return finish_blocked_call(_msgProcEntry, -1);                 // TEST25 ADD CLEANUP
        }

        {
            int _SendResult = _msgProcEntry->sendResult;
            return finish_blocked_call(_msgProcEntry, (_SendResult == -9999) ? 0 : _SendResult);   // TEST25 ADD CLEANUP
        }
    }

    // Slot mailbox block 
    if (_Mailbox->slotCount >= g_mailbox_maxSlots[mboxId])
    {
        if (!wait)
        {
            enableInterrupts();
            return -2;
        }

        int pid = k_getpid();
        MsgProcEntry* _msgProcEntry = mp_for_pid(pid);
        WaitingProcessPtr snode = wp_for_pid(pid);

        if (!_msgProcEntry || !snode)
        {
            enableInterrupts();
            return -1;
        }

        prepare_blocked_sender(_msgProcEntry, mboxId, pMsg, msg_size);         // TEST25 ADD CLEANUP

        // Init sender wait node
        snode->pid = pid;
        snode->pNextProcess = NULL;
        snode->pPrevProcess = NULL;

        waitq_push(&g_waitSendHead[mboxId], &g_waitSendTail[mboxId], snode);

        block(BLOCKED_SEND);

        disableInterrupts();

        // Mailbox release block
        if (_msgProcEntry->blockedMbox == mboxId && mailboxes[mboxId].status == MBSTATUS_RELEASED)
        {
            // TEST17 ADD confirm which child is the last to release
            // console_output(FALSE, "pid %d release decrement mailbox %d count from %d\n", k_getpid(), mboxId, g_releaseWaitCount[mboxId]);

            // One released waiter finished
            g_releaseWaitCount[mboxId]--;

            if (g_releaseWaitCount[mboxId] == 0 && g_releaseFreerPid[mboxId] >= 0)
            {
                // TEST17 ADD confirm which child is the last to release
                // console_output(FALSE, "pid %d unblocking freer pid %d for mailbox %d\n", k_getpid(), g_releaseFreerPid[mboxId], mboxId);

                // Wake freeing process last
                unblock(g_releaseFreerPid[mboxId]);
                g_releaseFreerPid[mboxId] = -1;
            }

            return finish_blocked_call(_msgProcEntry, -5);          // TEST25 ADD
        }

        // General signal path 
        if (signaled())
        {
            return finish_blocked_call(_msgProcEntry, -5);          // TEST25 ADD
        }

        if (_Mailbox->status != MBSTATUS_INUSE)
        {
            return finish_blocked_call(_msgProcEntry, -1);          // TEST25 ADD CLEANUP
        }

        {
            int _SendResult = _msgProcEntry->sendResult;
            return finish_blocked_call(_msgProcEntry, (_SendResult == -9999) ? 0 : _SendResult);   // TEST25 ADD CLEANUP
        }
    }

    // Space available in slotted mailbox: queue message 
    {
        SlotPtr _SlotPtr = allocate_slot();                         // TEST16 ALTER Allocate slot here instead of before block to avoid holding up a slot while blocked if mailbox is full
        if (!_SlotPtr)
        {
            enableInterrupts();
            // TEST09 failed to allocate slot for sender after blocking check output
            //console_output(FALSE, "No mail slots available.\n");
            stop(1);
        }

        // Fill message slot metadata
        _SlotPtr->mbox_id = mboxId;
        _SlotPtr->messageSize = msg_size;
        if (msg_size > 0)
        {
            memcpy(_SlotPtr->message, pMsg, (size_t)msg_size);             // TEST 11 ALTER Conditional copy to avoid invalid memcpy if msg_size is 0
        }

        slot_enqueue(mboxId, _SlotPtr);
        _Mailbox->slotCount++;

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

    if (mboxId < 0 || mboxId >= MAXMBOX)
    {
        return -1;
    }
    if (msg_size < 0)
    {
        return -1;
    }
    if (msg_size > 0 && pMsg == NULL)
    {
        return -1;
    }

    disableInterrupts();

    MailBox* _MailBox = &mailboxes[mboxId];
    if (_MailBox->status != MBSTATUS_INUSE)
    {
        enableInterrupts();
        return -1;
    }

    // First try queued mail slott mailbox block
    SlotPtr _SlotPtr = slot_dequeue(mboxId);
    if (_SlotPtr)
    {
        if (msg_size < _SlotPtr->messageSize)
        {
            // Put back at head
            _SlotPtr->pNextSlot = _MailBox->pSlotListHead;
            _SlotPtr->pPrevSlot = NULL;

            if (_MailBox->pSlotListHead)
            {
                _MailBox->pSlotListHead->pPrevSlot = _SlotPtr;
            }
            else
            {
                g_slotTail[mboxId] = _SlotPtr;
            }

            _MailBox->pSlotListHead = _SlotPtr;

            enableInterrupts();
            return -1;
        }

        {
            int _MsgSize = _SlotPtr->messageSize;
            if (_MsgSize > 0)
            {
                memcpy(pMsg, _SlotPtr->message, (size_t)_MsgSize);        // TEST 11 ALTER Conditional copy to avoid invalid memcpy if msg_size is 0
            }
            _MailBox->slotCount--;
            free_slot(_SlotPtr);

            /* For slotted mailboxes only: if a sender was blocked because mailbox was full,
               one slot just opened up, so queue one sender's pending message now. */
            if (g_mailbox_maxSlots[mboxId] > 0)
            {
                WaitingProcessPtr snode = waitq_pop(&g_waitSendHead[mboxId], &g_waitSendTail[mboxId]);
                if (snode)
                {
                    int spid = snode->pid;
                    MsgProcEntry* _msgProcEntry = mp_for_pid(spid);

                    if (_msgProcEntry && _MailBox->status == MBSTATUS_INUSE && _MailBox->slotCount < g_mailbox_maxSlots[mboxId])
                    {
                        // Requeue blocked sender message
                        SlotPtr _SlotPtr = allocate_slot();
                        if (_SlotPtr != NULL)
                        {
                            _SlotPtr->mbox_id = mboxId;
                            _SlotPtr->messageSize = _msgProcEntry->sendSize;
                            if (_msgProcEntry->sendSize > 0)
                            {
                                memcpy(_SlotPtr->message, _msgProcEntry->sendBuf, (size_t)_msgProcEntry->sendSize);
                            }

                            slot_enqueue(mboxId, _SlotPtr);
                            _MailBox->slotCount++;
                            _msgProcEntry->sendResult = 0;
                        }
                        else
                        {
                            enableInterrupts();
                            // TEST 16 ALTER If we fail to allocate a slot for the sender,
                            //console_output(FALSE, "No mail slots available.\n");
                            stop(1);
                        }
                    }
                    else if (_msgProcEntry)
                    {
                        _msgProcEntry->sendResult = -1;
                    }

                    unblock(spid);
                }
            }

            enableInterrupts();
            return _MsgSize;
        }
    }

    // Zero-slot mailbox: if a sender is already waiting, take directly from sender 
    // TEST29 ALTER zero-slot path before blocking to preserve rendezvous behavior for zero-slot mailboxes (instead of blocking sender and waiting for receiver to arrive, which would deadlock since receiver is what we're trying to unblock in the first place).
    if (g_mailbox_maxSlots[mboxId] == 0)
    {
        WaitingProcessPtr snode = waitq_pop(&g_waitSendHead[mboxId], &g_waitSendTail[mboxId]);
        if (snode)
        {
            int spid = snode->pid;
            MsgProcEntry* _msgProcEntry = mp_for_pid(spid);

            if (!_msgProcEntry || _msgProcEntry->sendSize < 0 || msg_size < _msgProcEntry->sendSize)
            {
                if (_msgProcEntry)
                {
                    _msgProcEntry->sendResult = -1;
                }

                unblock(spid);
                enableInterrupts();
                return -1;
            }

            if (_msgProcEntry->sendSize > 0)
            {
                memcpy(pMsg, _msgProcEntry->sendBuf, (size_t)_msgProcEntry->sendSize);
            }

            _msgProcEntry->sendResult = 0;
            unblock(spid);

            enableInterrupts();
            return _msgProcEntry->sendSize;
        }
    }

    if (!wait)
    {
        enableInterrupts();
        return -2;
    }

    // Block waiting receiver
    {
        int pid = k_getpid();
        MsgProcEntry* _MsgProcEntry = mp_for_pid(pid);
        WaitingProcessPtr node = wp_for_pid(pid);

        if (!_MsgProcEntry || !node)
        {
            enableInterrupts();
            return -1;
        }

        prepare_blocked_receiver(_MsgProcEntry, mboxId, pMsg, msg_size);   // CLEANUP ADD

        // Initialize receiver wait node
        node->pid = pid;
        node->pNextProcess = NULL;
        node->pPrevProcess = NULL;

        waitq_push(&g_waitRecvHead[mboxId], &g_waitRecvTail[mboxId], node);

        block(BLOCKED_RECEIVE);

        disableInterrupts();

        // Mailbox release block
        if (_MsgProcEntry->blockedMbox == mboxId && mailboxes[mboxId].status == MBSTATUS_RELEASED)
        {
            // One released waiter finished
            g_releaseWaitCount[mboxId]--;
            if (g_releaseWaitCount[mboxId] == 0 && g_releaseFreerPid[mboxId] >= 0)
            {
                // TEST17 ADD confirm which child is the last to release
                //console_output(FALSE, "pid %d unblocking freer pid %d for mailbox %d\n", k_getpid(), g_releaseFreerPid[mboxId], mboxId);

                // Wake freeing process last
                unblock(g_releaseFreerPid[mboxId]);
                g_releaseFreerPid[mboxId] = -1;
            }

            return finish_blocked_call(_MsgProcEntry, -5);                 // CLEANUP ADD
        }

        // General signal block
        if (signaled())
        {
            return finish_blocked_call(_MsgProcEntry, -5);                 // CLEANUP ADD
        }

        if (_MailBox->status != MBSTATUS_INUSE)
        {
            return finish_blocked_call(_MsgProcEntry, -1);                 // CLEANUP ADD
        }

        {
            int result = _MsgProcEntry->recvResult;
            return finish_blocked_call(_MsgProcEntry, result);             // CLEANUP ADD
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

    // TEST25 MAXMBOX Sanity Check
    //console_output(FALSE, "DEBUG mailbox_free called with mboxId=%d\n", mboxId);

    if (mboxId < 0 || mboxId >= MAXMBOX)
    {
        return -1;
    }

    disableInterrupts();

    MailBox* _MailBox = &mailboxes[mboxId];
    if (_MailBox->status != MBSTATUS_INUSE)
    {
        enableInterrupts();
        return -1;
    }

    // Mark released first so blocked send/recv paths detect closure
    _MailBox->status = MBSTATUS_RELEASED;

    SlotPtr _SlotPtr = _MailBox->pSlotListHead;
    while (_SlotPtr != NULL)
    {
        SlotPtr next = _SlotPtr->pNextSlot;
        free_slot(_SlotPtr);
        _SlotPtr = next;
    }

    _MailBox->pSlotListHead = NULL;
    g_slotTail[mboxId] = NULL;
    _MailBox->slotCount = 0;

    // Wake all blocked receivers and senders
    int awakened = 0;
    int waitPids[MAXPROC];
    int waitTypes[MAXPROC];
    WaitingProcessPtr node;

    while ((node = waitq_pop(&g_waitRecvHead[mboxId], &g_waitRecvTail[mboxId])) != NULL)
    {
        if (awakened < MAXPROC)
        {
            // Save released receiver pid
            waitPids[awakened] = node->pid;
            waitTypes[awakened] = BLOCKED_RECEIVE;
            awakened++;
        }
    }

    while ((node = waitq_pop(&g_waitSendHead[mboxId], &g_waitSendTail[mboxId])) != NULL)
    {
        if (awakened < MAXPROC)
        {
            // Save released sender pid
            waitPids[awakened] = node->pid;
            waitTypes[awakened] = BLOCKED_SEND;
            awakened++;
        }
    }

    // TEST09 - TEST17 DEBUG
    //console_output(FALSE, "mailbox_free(%d): awakened=%d\n", mboxId, awakened);

    // IMPORTANT initialize release handshake BEFORE unblocking anyone 
    if (awakened > 0)
    {
        // Track awakened release waiters
        g_releaseWaitCount[mboxId] = awakened;
        g_releaseFreerPid[mboxId] = k_getpid();
    }

    for (int i = 0; i < awakened; i++)
    {
        int pid = waitPids[i];
        MsgProcEntry* _MsgProcEntry = mp_for_pid(pid);

        // TEST09 DEBUG  show who is being released
        //console_output(FALSE, "mailbox_free(%d): releasing pid=%d type=%s\n", mboxId, pid, (waitTypes[i] == BLOCKED_SEND) ? "SEND" : "RECEIVE");

        if (_MsgProcEntry)
        {
            // Mark process as released
            _MsgProcEntry->blockedMbox = mboxId;
            _MsgProcEntry->blockedType = waitTypes[i];
        }

        k_kill(pid, SIG_TERM);
        unblock(pid);
    }

    // If waiters remain, block until last releaser wakes us
    if (awakened > 0 && g_releaseWaitCount[mboxId] > 0)
    {
        block(BLOCKED_RELEASE);
        disableInterrupts();
    }

    // Reset mailbox state so it can be reused by mailbox_create()
    _MailBox->pSlotListHead = NULL;
    g_slotTail[mboxId] = NULL;
    _MailBox->mbox_id = mboxId;
    _MailBox->slotSize = 0;
    _MailBox->slotCount = 0;
    _MailBox->type = MB_MAXTYPES;
    _MailBox->status = MBSTATUS_EMPTY;

    // Clear mailbox runtime state
    g_mailbox_maxSlots[mboxId] = 0;
    g_waitRecvHead[mboxId] = NULL;
    g_waitRecvTail[mboxId] = NULL;
    g_waitSendHead[mboxId] = NULL;
    g_waitSendTail[mboxId] = NULL;
    g_releaseWaitCount[mboxId] = 0;
    g_releaseFreerPid[mboxId] = -1;

    enableInterrupts();

    if (signaled())
    {
        return -5;
    }

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
    int _DeviceHandle = -1;              // Use int for consistency with device APIs
    checkKernelMode("wait_device");

    // Basic parameter validation 
    if (deviceName == NULL || status == NULL)
    {
        return -1;
    }

    // Allow interrupts while waiting for device interrupt 
    enableInterrupts();

    if (strcmp(deviceName, "clock") == 0)
    {
        _DeviceHandle = THREADS_CLOCK_DEVICE_ID;
    }
    else
    {
        _DeviceHandle = device_handle(deviceName);

        // TEST05 ADD Ensure the device was properly initialized and handle is valid
        if (_DeviceHandle < 0 || _DeviceHandle >= THREADS_MAX_DEVICES)
        {
            console_output(FALSE, "wait_device: Unknown or uninitialized device %s.\n", deviceName);
            stop(-1);
        }
    }

    if (_DeviceHandle >= 0 && _DeviceHandle < THREADS_MAX_DEVICES)
    {
        // Make sure this device has a mailbox 
        if (devices[_DeviceHandle].deviceMbox < 0)
        {
            console_output(FALSE, "wait_device: No mailbox for device %s (handle %d).\n", deviceName, _DeviceHandle);
            stop(-1);
        }

        // set a flag that there is a process waiting on a device. 
        waitingOnDevice++;

        // TEST05 ADD - Adding check for possible failure 
        int _MailStatus = mailbox_receive(devices[_DeviceHandle].deviceMbox, status, sizeof(int), TRUE);

        if (_MailStatus < 0)
        {
            result = _MailStatus;   // return mailbox failure
        }

        // Re-disable interrupts after returning from block 
        disableInterrupts();
        waitingOnDevice--;
    }
    else
    {
        console_output(FALSE, "Unknown device type.");
        stop(-1);
    }

    // If process was signaled while waiting, return -5 
    if (signaled())
    {
        result = -5;
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

static void InitializeHandlers(void)
{
    handlers = get_interrupt_handlers();

    // Also init the system call vector(systemCallVector)
    // TEST05 ADD Using syscall with nullsys by default                    
    for (int i = 0; i < THREADS_MAX_SYSCALLS; i++)
    {
        systemCallVector[i] = nullsys;
    }

    /* TODO: Register interrupt handlers in the handlers array.
     * Use the interrupt indices defined in THREADSLib.h:
     *   handlers[THREADS_TIMER_INTERRUPT]   = your_clock_handler;
     *   handlers[THREADS_IO_INTERRUPT]      = your_io_handler;
     *   handlers[THREADS_SYS_CALL_INTERRUPT] = your_syscall_handler;*/

    handlers[THREADS_TIMER_INTERRUPT] = clock_handler_messaging;            // TEST09 ADD - In lecture new clock handler
    handlers[THREADS_IO_INTERRUPT] = io_handler;                            // TEST05 ADD
    handlers[THREADS_SYS_CALL_INTERRUPT] = syscall_handler;                 // TEST05 ADD
}

// an error method to handle invalid syscalls 
static void nullsys(system_call_arguments_t* args)
{
    console_output(FALSE, "nullsys(): Invalid syscall %d. Halting...\n", args->call_id);
    stop(1);
}

static void io_handler(char deviceId[32], uint8_t command, uint32_t status, void* pArgs)
{
    (void)command;
    (void)pArgs;

    // TEST05 ADD - Convert deviceId parameter into device index safely
    int idx = device_id_from_param(deviceId);

    if (idx < 0 || idx >= THREADS_MAX_DEVICES)
    {
        return;
    }

    // TEST08 ADD If this device wasn't initialized, ignore the interrupt safely
    if (devices[idx].deviceMbox < 0)
    {
        return;
    }

    int st = (int)status;

    // Interrupt context: must be non-blocking 
    mailbox_send(devices[idx].deviceMbox, &st, sizeof(int), FALSE);
}

// TEST09 ADD - New clock handler for Messaging project (per lecture)
// NOTE Do NOT name this clock_handler (symbol already exists in Scheduler project)
static void clock_handler_messaging(char deviceId[32], uint8_t command, uint32_t status, void* pArgs)
{
    (void)command;
    (void)status;
    (void)pArgs;

    // In this THREADS build, deviceId is effectively a numeric device index 
    int idx = (int)(uintptr_t)deviceId;

    // Validate index 
    if (idx < 0 || idx >= THREADS_MAX_DEVICES)
    {
        return;
    }

    // Must still time slice for round-robin scheduling 
    time_slice();

    // TEST08 ADD Every 5th clock interrupt, send a tick to the clock mailbox (non-blocking) */
    static int _TickCount = 0;
    _TickCount++;

    if ((_TickCount % 5) == 0)
    {
        // Clock mailbox should be at devices[THREADS_CLOCK_DEVICE_ID] 
        int clockIdx = THREADS_CLOCK_DEVICE_ID;

        // If the clock mailbox isn't initialized, just skip safely 
        if (devices[clockIdx].deviceMbox < 0)
        {
            return;
        }

        // "Tick" message content doesn't really matter; presence matters 
        int tick = _TickCount;

        // Interrupt context: must be non-blocking 
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

/*****************************************************************************
   Name - checkKernelMode
   Purpose - Checks the PSR for kernel mode and halts if in user mode
   Parameters -
   Returns -
****************************************************************************/
static inline void checkKernelMode(const char* functionName)
{
    (void)functionName;

    union psr_values psrValue;

    psrValue.integer_part = get_psr();
    if (psrValue.bits.cur_mode == 0)
    {
        console_output(FALSE, "Kernel mode expected, but function called in user mode.\n");
        stop(1);
    }
}