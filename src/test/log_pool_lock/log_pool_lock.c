/*
 * Copyright 2015-2016, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *     * Neither the name of the copyright holder nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * log_pool_lock.c -- unit test which checks whether it's possible to
 *                    simultaneously open the same log pool
 */

#include "unittest.h"

#ifdef _WIN32
#include <tchar.h>
#include <Strsafe.h>
/*
 * So this is not like really a fork at all but overloading for testing using
 * CreateProcess works just fine.
 */
int
test_process(const char *path, int sleep)
{
	STARTUPINFO statusInfo;
	PROCESS_INFORMATION procInfo;
	TCHAR cmd[MAX_PATH] = TEXT("..\\..\\x64\\debug\\log_pool_lock.exe ");
	TCHAR parm[MAX_PATH] = TEXT("");

	/* build the cmd to start a 2nd test process */
	int nChars = MultiByteToWideChar(CP_ACP, 0, path, -1, NULL, 0);
	MultiByteToWideChar(CP_ACP, 0, path, -1, parm, nChars);
	_tcscat(cmd, parm);
	_tcscat(cmd, L" X");

	ZeroMemory(&statusInfo, sizeof(statusInfo));
	statusInfo.cb = sizeof(statusInfo);
	ZeroMemory(&procInfo, sizeof(procInfo));

	/* start the 2nd test process */
	if (!CreateProcess(NULL,
		cmd,
		NULL,
		NULL,
		FALSE,
		0,
		NULL,
		NULL,
		&statusInfo,
		&procInfo)) {
		return 0;
	}

	WaitForSingleObject(procInfo.hProcess, INFINITE);
	CloseHandle(procInfo.hProcess);
	CloseHandle(procInfo.hThread);
	return 1;
}
#endif

static void
test_reopen(const char *path)
{
	PMEMlogpool *log1 = pmemlog_create(path, PMEMLOG_MIN_POOL,
		S_IWUSR | S_IRUSR);
	if (!log1)
		UT_FATAL("!create");

	PMEMlogpool *log2 = pmemlog_open(path);
	if (log2)
		UT_FATAL("pmemlog_open should not succeed");

	if (errno != EWOULDBLOCK)
		UT_FATAL("!pmemlog_open failed but for unexpected reason");

	pmemlog_close(log1);

	log2 = pmemlog_open(path);
	if (!log2)
		UT_FATAL("pmemlog_open should succeed after close");

	pmemlog_close(log2);

	UNLINK(path);
}

#ifndef _WIN32
static void
test_open_in_different_process(const char *path, int sleep)
{
	pid_t pid = fork();
	PMEMlogpool *log;

	if (pid < 0)
		UT_FATAL("fork failed");

	if (pid == 0) {
		/* child */
		if (sleep)
			usleep(sleep);
		while (access(path, R_OK))
			usleep(100 * 1000);

		log = pmemlog_open(path);
		if (log)
			UT_FATAL("pmemlog_open after fork should not succeed");

		if (errno != EWOULDBLOCK)
			UT_FATAL("!pmemlog_open after fork failed but for "
				"unexpected reason");

		exit(0);
	}

	log = pmemlog_create(path, PMEMLOG_MIN_POOL, S_IWUSR | S_IRUSR);
	if (!log)
		UT_FATAL("!create");

	int status;

	if (waitpid(pid, &status, 0) < 0)
		UT_FATAL("!waitpid failed");

	if (!WIFEXITED(status))
		UT_FATAL("child process failed");

	pmemlog_close(log);

	UNLINK(path);
}
#else
static void
test_open_in_different_process(const char *path, int sleep)
{
	PMEMlogpool *log;

	if (sleep > 0)
		return;

	/* before starting the 2nd process, create a pool */
	log = pmemlog_create(path, PMEMLOG_MIN_POOL, S_IWUSR | S_IRUSR);
	if (!log)
		UT_FATAL("!create");

	if (!test_process(path, sleep))
		UT_FATAL("CreateProcess failed error: %d", GetLastError());

	pmemlog_close(log);
}
#endif

int
main(int argc, char *argv[])
{
	START(argc, argv, "log_pool_lock");

	if (argc < 2)
		UT_FATAL("usage: %s path", argv[0]);

	if (argc == 2) {
		test_reopen(argv[1]);

		test_open_in_different_process(argv[1], 0);
		for (int i = 1; i < 100000; i *= 2)
			test_open_in_different_process(argv[1], i);
	} else if (argc == 3) {
		PMEMlogpool *log;
		/* 2nd arg used by windows for 2 process test */
		log = pmemlog_open(argv[1]);
		if (log)
			UT_FATAL("pmemlog_open after CreateProcess should "
				"not succeed");

		if (errno != EWOULDBLOCK)
			UT_FATAL("!pmemlog_open after CreateProcess failed "
				"but for unexpected reason");
	}


	DONE(NULL);
}
