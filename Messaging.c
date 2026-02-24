///////////////////////////////////////////////////////////////////////////
//   Messaging.c
//   College of Applied Science and Technology
//   The University of Arizona
//   CYBV 489
//
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

/* ------------------------- Prototypes ----------------------------------- */
static void nullsys(system_call_arguments_t* args);

/* Note: interrupt_handler_t is already defined in THREADSLib.h with the signature:
 *   void (*)(char deviceId[32], uint8_t command, uint32_t status, void *pArgs)
 */
static void InitializeHandlers();
static int check_io_messaging(void);
extern int MessagingEntryPoint(void*);
static void checkKernelMode(const char* functionName);

//////////// GLOBAL VARIABLES ////////////
static int g_mbox_inuse[MAXMBOX];               // 0 = Free, 1 = Used   TEST02 ADD
static int g_mbox_slots[MAXMBOX];               // MAX slots requested   TEST02 ADD 
static int g_mbox_slot_size[MAXMBOX];           // MAX bytes per message    TEST02 ADD
static SlotPtr freeSlotHead = NULL;             // Slot free list management   TEST02 ADD
static int g_mbox_maxSlots[MAXMBOX];            // TEST03 ADD
//////////////////////////////////////////

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

/* -------------------------- Globals ------------------------------------- */
/* Obtained from THREADS*/
interrupt_handler_t* handlers;
/* system call array of function pointers */
void (*systemCallVector[THREADS_MAX_SYSCALLS])(system_call_arguments_t* args);
/* the mail boxes */
MailBox mailboxes[MAXMBOX];
MailSlot mailSlots[MAXSLOTS];

typedef struct
{
    void* deviceHandle;
    int deviceMbox;
    int deviceType;
    char deviceName[16];
} DeviceManagementData;

static DeviceManagementData devices[THREADS_MAX_DEVICES];
static int nextMailboxId = 0;
static int waitingOnDevice = 0;

/////////// FUNCTION PROTOTYPES /////////////////////////
static void init_slot_freelist(void);       // TEST03 ADD
static SlotPtr alloc_slot(void);            // TEST03 ADD
static void free_slot(SlotPtr s);           // TEST03 ADD
static void init_mailboxes(void);           // TEST03 ADD
/////////////////////////////////////////////////////////


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
      // TODO: Create mailboxes for each device.
      //   devices[THREADS_CLOCK_DEVICE_ID].deviceMbox = mailbox_create(0, sizeof(int));
      //   devices[i].deviceMbox = mailbox_create(..., sizeof(int));

      /* TODO: Initialize the devices using device_initialize().
       * The devices are: disk0, disk1, term0, term1, term2, term3.
       * Store the device handle and name in the devices array.
       */


    init_mailboxes();           // TEST03 ADD
    init_slot_freelist();       // TEST03 ADD

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
            g_mbox_maxSlots[i] = slots;     // TEST03 ADD
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
    // TEST03 ADD Validate parameters and mailbox state.
    if (mboxId < 0 || mboxId >= MAXMBOX) return -1;
    if (pMsg == NULL) return -1;
    if (msg_size < 0) return -1;

    disableInterrupts();

    MailBox* _mailbox = &mailboxes[mboxId];
    if (_mailbox->status != MBSTATUS_INUSE)
    {
        enableInterrupts();
        return -1;
    }

    if (msg_size > _mailbox->slotSize || msg_size > MAX_MESSAGE)
    {
        enableInterrupts();
        return -1;
    }

    /* TEST03 ADD enforce capacity */
    if (g_mbox_maxSlots[mboxId] > 0 && _mailbox->slotCount >= g_mbox_maxSlots[mboxId])
    {
        /* slots are full */
        enableInterrupts();
        return wait ? -2 : -2;
    }

    SlotPtr _slotptr = alloc_slot();
    if (_slotptr == NULL)
    {
        enableInterrupts();
        return -1;          // Out of slots
    }

    _slotptr->mbox_id = mboxId;
    _slotptr->messageSize = msg_size;
    memcpy(_slotptr->message, pMsg, (size_t)msg_size);

    /* push onto mailbox slot list head */
    _slotptr->pNextSlot = _mailbox->pSlotListHead;
    _slotptr->pPrevSlot = NULL;
    if (_mailbox->pSlotListHead)
        _mailbox->pSlotListHead->pPrevSlot = _slotptr;
    _mailbox->pSlotListHead = _slotptr;

    _mailbox->slotCount++;

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
    // TEST03 ADD: Validate parameters and mailbox state.
    if (mboxId < 0 || mboxId >= MAXMBOX) return -1;
    if (pMsg == NULL) return -1;
    if (msg_size < 0) return -1;

    disableInterrupts();

    MailBox* _mailbox = &mailboxes[mboxId];
    if (_mailbox->status != MBSTATUS_INUSE)
    {
        enableInterrupts();
        return -1;
    }

    SlotPtr _slotptr = _mailbox->pSlotListHead;
    if (_slotptr == NULL)
    {
        enableInterrupts();
        return wait ? -2 : -2;
    }

    if (msg_size < _slotptr->messageSize)
    {
        enableInterrupts();
        return -1;          /* buffer too small */
    }

    /* pop from head of list */
    _mailbox->pSlotListHead = _slotptr->pNextSlot;
    if (_mailbox->pSlotListHead)
    {
        _mailbox->pSlotListHead->pPrevSlot = NULL;
    }

    _mailbox->slotCount--;

    int _messize = _slotptr->messageSize;
    memcpy(pMsg, _slotptr->message, (size_t)_messize);

    free_slot(_slotptr);

    enableInterrupts();
    return _messize;
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
    uint32_t deviceHandle = -1;
    checkKernelMode("waitdevice");

    enableInterrupts();

    if (strcmp(deviceName, "clock") == 0)
    {
        deviceHandle = THREADS_CLOCK_DEVICE_ID;
    }
    else
    {
        deviceHandle = device_handle(deviceName);
    }

    if (deviceHandle >= 0 && deviceHandle < THREADS_MAX_DEVICES)
    {
        /* set a flag that there is a process waiting on a device. */
        waitingOnDevice++;
        mailbox_receive(devices[deviceHandle].deviceMbox, status, sizeof(int), TRUE);
        disableInterrupts();
        waitingOnDevice--;
    }
    else
    {
        console_output(FALSE, "Unknown device type.");
        stop(-1);
    }

    /* spec says return -5 if signaled. */
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

static void InitializeHandlers()
{
    handlers = get_interrupt_handlers();

    /* TODO: Register interrupt handlers in the handlers array.
     * Use the interrupt indices defined in THREADSLib.h:
     *   handlers[THREADS_TIMER_INTERRUPT]   = your_clock_handler;
     *   handlers[THREADS_IO_INTERRUPT]      = your_io_handler;
     *   handlers[THREADS_SYS_CALL_INTERRUPT] = your_syscall_handler;
     *
     * Also initialize the system call vector (systemCallVector).
     */
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

static SlotPtr alloc_slot(void)
{
    SlotPtr _slotptr= freeSlotHead;
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

static void free_slot(SlotPtr _slotptr)
{
    if (!_slotptr) return;
    _slotptr->pNextSlot = freeSlotHead;
    _slotptr->pPrevSlot = NULL;
    _slotptr->mbox_id = -1;
    _slotptr->messageSize = 0;
    freeSlotHead = _slotptr;
}

static void init_mailboxes(void)
{
    for (int i = 0; i < MAXMBOX; i++)
    {
        mailboxes[i].pSlotListHead = NULL;
        mailboxes[i].mbox_id = i;
        mailboxes[i].type = MB_MAXTYPES;          /* unknown until create */
        mailboxes[i].status = MBSTATUS_EMPTY;     /* not allocated yet */
        mailboxes[i].slotSize = 0;
        mailboxes[i].slotCount = 0;

        g_mbox_maxSlots[i] = 0;         // TEST03 ADD: initialize maxSlots array to 0 for all mailboxes
    }

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