#include "ghostclock/core.h"
#include "ghostclock/windows.h"

#ifdef _WIN32

#include <stdio.h>
#include <string.h>

static int gc_child_mode(void)
{
    Sleep(30000);
    return 0;
}

int main(int argc, char **argv)
{
    char executable[MAX_PATH];
    char command_line[(MAX_PATH * 2) + 32];
    char session_id[GC_SESSION_ID_CAPACITY];
    STARTUPINFOA startup;
    PROCESS_INFORMATION child;
    gc_win32_process process;
    gc_win32_error error;
    gc_metrics metrics;
    gc_priority original_priority;
    gc_priority observed_priority;
    gc_session session;
    DWORD executable_length;
    int command_length;
    int result = 1;

    if (argc == 2 && strcmp(argv[1], "--child") == 0) {
        return gc_child_mode();
    }

    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    memset(&child, 0, sizeof(child));
    memset(&process, 0, sizeof(process));
    memset(&error, 0, sizeof(error));
    memset(&session, 0, sizeof(session));

    executable_length = GetModuleFileNameA(NULL, executable, (DWORD)sizeof(executable));
    if (executable_length == 0 || executable_length >= sizeof(executable)) {
        fprintf(stderr, "GetModuleFileNameA failed: Win32 error %lu\n", (unsigned long)GetLastError());
        return 1;
    }

    command_length = snprintf(command_line, sizeof(command_line), "\"%s\" --child", executable);
    if (command_length < 0 || (size_t)command_length >= sizeof(command_line)) {
        fprintf(stderr, "Child command line exceeded its buffer\n");
        return 1;
    }

    if (!CreateProcessA(
            NULL,
            command_line,
            NULL,
            NULL,
            FALSE,
            CREATE_NEW_PROCESS_GROUP | NORMAL_PRIORITY_CLASS,
            NULL,
            NULL,
            &startup,
            &child)) {
        fprintf(stderr, "CreateProcessA failed: Win32 error %lu\n", (unsigned long)GetLastError());
        return 1;
    }

    CloseHandle(child.hThread);

    if (!gc_win32_open_process(child.dwProcessId, true, &process, &error)) {
        fprintf(stderr, "%s\n", gc_win32_error_text(&error));
        goto cleanup;
    }
    if (!gc_win32_read_metrics(&process, &metrics, &original_priority, &error)) {
        fprintf(stderr, "%s\n", gc_win32_error_text(&error));
        goto cleanup;
    }
    if (original_priority != GC_PRIORITY_NORMAL) {
        fprintf(stderr, "Expected Normal priority, observed %s\n", gc_priority_name(original_priority));
        goto cleanup;
    }

    gc_win32_make_session_id(session_id);
    if (gc_session_init(
            &session,
            session_id,
            "ghostclock_tests.exe",
            (uint32_t)child.dwProcessId,
            GC_MODE_BOOST,
            GC_PROFILE_BALANCED) != GC_OK ||
        gc_session_snapshot_priority(&session, original_priority) != GC_OK ||
        gc_session_plan_priority(&session, GC_PRIORITY_ABOVE_NORMAL) != GC_OK) {
        fprintf(stderr, "Session setup failed\n");
        goto cleanup;
    }

    if (!gc_win32_set_priority(&process, GC_PRIORITY_ABOVE_NORMAL, &error)) {
        fprintf(stderr, "%s\n", gc_win32_error_text(&error));
        goto cleanup;
    }
    if (gc_session_mark_priority_applied(&session) != GC_OK || gc_session_start(&session) != GC_OK) {
        fprintf(stderr, "Session apply transition failed\n");
        goto rollback;
    }
    if (!gc_win32_read_metrics(&process, &metrics, &observed_priority, &error) ||
        observed_priority != GC_PRIORITY_ABOVE_NORMAL) {
        fprintf(stderr, "Priority change was not observable\n");
        goto rollback;
    }

    printf("[INFO] demo priority changed from=Normal to=Above Normal\n");

rollback:
    if (session.rollback_status == GC_ROLLBACK_PENDING) {
        if (gc_session_begin_rollback(&session) != GC_OK ||
            !gc_win32_set_priority(&process, original_priority, &error) ||
            gc_session_mark_priority_restored(&session) != GC_OK) {
            fprintf(stderr, "Rollback failed: %s\n", gc_win32_error_text(&error));
            goto cleanup;
        }
    }

    if (!gc_win32_read_metrics(&process, &metrics, &observed_priority, &error) ||
        observed_priority != original_priority) {
        fprintf(stderr, "Restored priority did not match the snapshot\n");
        goto cleanup;
    }
    if (gc_session_complete(&session) != GC_OK ||
        session.rollback_status != GC_ROLLBACK_RESTORED) {
        fprintf(stderr, "Session did not complete with restored rollback state\n");
        goto cleanup;
    }

    printf("[INFO] rollback completed priority=Normal\n");
    result = 0;

cleanup:
    if (process.handle != NULL) {
        gc_win32_close_process(&process);
    }
    if (child.hProcess != NULL) {
        TerminateProcess(child.hProcess, 0);
        WaitForSingleObject(child.hProcess, 5000);
        CloseHandle(child.hProcess);
    }
    return result;
}

#endif
