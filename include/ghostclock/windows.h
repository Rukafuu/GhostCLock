#ifndef GHOSTCLOCK_WINDOWS_H
#define GHOSTCLOCK_WINDOWS_H

#ifdef _WIN32

#include "ghostclock/core.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <windows.h>

#define GC_PROCESS_CANDIDATE_CAPACITY 16
#define GC_WIN32_ERROR_CONTEXT_CAPACITY 160
#define GC_WIN32_ERROR_MESSAGE_CAPACITY 512

typedef struct gc_win32_error {
    DWORD code;
    char context[GC_WIN32_ERROR_CONTEXT_CAPACITY];
    char message[GC_WIN32_ERROR_MESSAGE_CAPACITY];
} gc_win32_error;

typedef struct gc_process_candidate {
    DWORD pid;
    DWORD thread_count;
} gc_process_candidate;

typedef struct gc_process_candidates {
    gc_process_candidate items[GC_PROCESS_CANDIDATE_CAPACITY];
    size_t stored_count;
    size_t total_count;
} gc_process_candidates;

typedef struct gc_win32_process {
    HANDLE handle;
    DWORD pid;
} gc_win32_process;

bool gc_win32_utf8_to_wide(
    const char *input,
    wchar_t *output,
    size_t output_capacity,
    gc_win32_error *error);
bool gc_win32_find_processes(
    const wchar_t *executable_name,
    gc_process_candidates *candidates,
    gc_win32_error *error);
bool gc_win32_open_process(
    DWORD pid,
    bool allow_priority_change,
    gc_win32_process *process,
    gc_win32_error *error);
void gc_win32_close_process(gc_win32_process *process);
bool gc_win32_read_metrics(
    const gc_win32_process *process,
    gc_metrics *metrics,
    gc_priority *priority,
    gc_win32_error *error);
bool gc_win32_set_priority(
    const gc_win32_process *process,
    gc_priority priority,
    gc_win32_error *error);
bool gc_win32_is_process_active(
    const gc_win32_process *process,
    bool *active,
    gc_win32_error *error);
DWORD gc_win32_wait(const gc_win32_process *process, DWORD timeout_ms);
uint32_t gc_win32_logical_processor_count(void);
void gc_win32_make_session_id(char output[GC_SESSION_ID_CAPACITY]);
const char *gc_win32_error_text(const gc_win32_error *error);

#endif

#endif
