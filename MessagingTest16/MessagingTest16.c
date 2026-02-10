#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <string.h>
#include "THREADSLib.h"
#include "Scheduler.h"
#include "Messaging.h"
#include "TestCommon.h"

/*********************************************************************************
*
* MessagingTest16
*
* Double free and post-free operation validation test.
*
* Verifies that calling mailbox_free on an already-freed mailbox returns -1,
* and that send/receive operations on a freed mailbox also return -1. Tests
* multiple scenarios:
*
*   1) Create a mailbox, send a message to it (leaving unread mail), free it.
*      Verify free returns 0.
*   2) Free the same mailbox again. Verify the second free returns -1.
*   3) Attempt to send to the freed mailbox. Verify it returns -1.
*   4) Attempt to receive from the freed mailbox. Verify it returns -1.
*   5) Create a second mailbox, free it with no messages, then double free.
*      Verify second free returns -1.
*   6) Create a third mailbox, verify it works normally after all the
*      invalid operations above (no table corruption).
*
* This test uses only the parent process (no child spawns). All sends and
* receives use non-blocking mode.
*
* Expected output:
*   - First free on each mailbox returns 0.
*   - Second free on each mailbox returns -1.
*   - Send/receive on freed mailbox returns -1.
*   - A newly created mailbox works correctly after all invalid operations.
*
* Functions tested:
*   mailbox_create, mailbox_send, mailbox_receive, mailbox_free
*
* Coverage gap filled:
*   No prior test calls mailbox_free twice on the same mailbox. Tests 19/20
*   verify send/receive after free, but never attempt a second free. This
*   test ensures the mailbox table correctly marks freed entries and rejects
*   redundant free calls.
*
*********************************************************************************/
int MessagingEntryPoint(void* pArgs)
{
	char* testName = GetTestName(__FILE__);
	int result;
	int mbox1, mbox2, mbox3;
	char message[50];
	int passCount = 0;
	int testCount = 0;

	console_output(FALSE, "\n%s: started\n", testName);

	/* --- Scenario 1: Create mailbox, send a message, free it --- */
	mbox1 = mailbox_create(5, 50);
	console_output(FALSE, "%s: mailbox_create returned id = %d\n", testName, mbox1);

	strcpy(message, "Unread message");
	result = mailbox_send(mbox1, message, (int)strlen(message) + 1, FALSE);
	console_output(FALSE, "%s: mailbox_send returned %d\n", testName, result);

	result = mailbox_free(mbox1);
	testCount++;
	if (result == 0)
	{
		console_output(FALSE, "%s: First free of mailbox %d returned 0: PASSED\n", testName, mbox1);
		passCount++;
	}
	else
	{
		console_output(FALSE, "%s: First free of mailbox %d returned %d (expected 0): FAILED\n",
			testName, mbox1, result);
	}

	/* --- Scenario 2: Double free --- */
	result = mailbox_free(mbox1);
	testCount++;
	if (result == -1)
	{
		console_output(FALSE, "%s: Second free of mailbox %d returned -1: PASSED\n", testName, mbox1);
		passCount++;
	}
	else
	{
		console_output(FALSE, "%s: Second free of mailbox %d returned %d (expected -1): FAILED\n",
			testName, mbox1, result);
	}

	/* --- Scenario 3: Send to freed mailbox --- */
	strcpy(message, "After free");
	result = mailbox_send(mbox1, message, (int)strlen(message) + 1, FALSE);
	testCount++;
	if (result == -1)
	{
		console_output(FALSE, "%s: Send to freed mailbox %d returned -1: PASSED\n", testName, mbox1);
		passCount++;
	}
	else
	{
		console_output(FALSE, "%s: Send to freed mailbox %d returned %d (expected -1): FAILED\n",
			testName, mbox1, result);
	}

	/* --- Scenario 4: Receive from freed mailbox --- */
	memset(message, 0, sizeof(message));
	result = mailbox_receive(mbox1, message, sizeof(message), FALSE);
	testCount++;
	if (result == -1)
	{
		console_output(FALSE, "%s: Receive from freed mailbox %d returned -1: PASSED\n", testName, mbox1);
		passCount++;
	}
	else
	{
		console_output(FALSE, "%s: Receive from freed mailbox %d returned %d (expected -1): FAILED\n",
			testName, mbox1, result);
	}

	/* --- Scenario 5: Empty mailbox double free --- */
	mbox2 = mailbox_create(10, 50);
	console_output(FALSE, "\n%s: mailbox_create returned id = %d\n", testName, mbox2);

	result = mailbox_free(mbox2);
	testCount++;
	if (result == 0)
	{
		console_output(FALSE, "%s: First free of empty mailbox %d returned 0: PASSED\n", testName, mbox2);
		passCount++;
	}
	else
	{
		console_output(FALSE, "%s: First free of empty mailbox %d returned %d (expected 0): FAILED\n",
			testName, mbox2, result);
	}

	result = mailbox_free(mbox2);
	testCount++;
	if (result == -1)
	{
		console_output(FALSE, "%s: Second free of empty mailbox %d returned -1: PASSED\n", testName, mbox2);
		passCount++;
	}
	else
	{
		console_output(FALSE, "%s: Second free of empty mailbox %d returned %d (expected -1): FAILED\n",
			testName, mbox2, result);
	}

	/* --- Scenario 6: New mailbox still works after all invalid ops --- */
	mbox3 = mailbox_create(5, 50);
	console_output(FALSE, "\n%s: mailbox_create returned id = %d (post-validation mailbox)\n", testName, mbox3);

	strcpy(message, "Sanity check");
	result = mailbox_send(mbox3, message, (int)strlen(message) + 1, FALSE);
	testCount++;
	if (result == 0)
	{
		console_output(FALSE, "%s: Send to new mailbox succeeded: PASSED\n", testName);
		passCount++;
	}
	else
	{
		console_output(FALSE, "%s: Send to new mailbox returned %d: FAILED\n", testName, result);
	}

	memset(message, 0, sizeof(message));
	result = mailbox_receive(mbox3, message, sizeof(message), FALSE);
	testCount++;
	if (result >= 0)
	{
		console_output(FALSE, "%s: Receive from new mailbox succeeded, message = %s: PASSED\n",
			testName, message);
		passCount++;
	}
	else
	{
		console_output(FALSE, "%s: Receive from new mailbox returned %d: FAILED\n", testName, result);
	}

	console_output(FALSE, "\n%s: Results: %d / %d passed\n", testName, passCount, testCount);

	mailbox_free(mbox3);

	k_exit(0);
	return 0;
}
