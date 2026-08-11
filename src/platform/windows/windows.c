#include "ghostclock/windows.h"

#ifdef _WIN32

#include <limits.h>
#include <psapi.h>
#include <stdio.h>
#include <string.h>
#include <tlhelp32.h>
#include <wchar.h>

static uint64_t gc_filetime_to_u64(FILETIME value)
{
    ULARGE_INTEGER converted;
    converted.LowPart = value.dwLowDateTime;
    converted.HighPart = value.dwHighDateTime;
    return converted.QuadPart;
}

static void gc_win32_set_error(gc_win32_error *error, const char *context, DWORD code)
{
    DWORD length;

    if (error == NULL) {
        return;
    }

    memset(error, 0, sizeof(*error));
    error->code = code;
    if (context != NULL) {
        snprintf(error->context, sizeof(error->context), "%s", context);
    }

    length = FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        error->message,
        (DWORD)sizeof(error->message),
        NULL);

    if (length == 0) {
        snprintf(error->message, sizeof(error->message), "Win32 error %lu", (unsigned long)code);
        return;
    }

    while (length > 0 &&
           (error->message[length - 1] == '\r' || error->message[length - 1] == '\n' ||
            error->message[length - 1] == ' ' || error->message[length - 1] == '.')) {
        error->message[--length] = '\0';
    }
}

static gc_priority gc_priority_from_win32(DWORD value)
{
    switch (value) {
    case IDLE_PRIORITY_CLASS:
        return GC_PRIORITY_IDLE;
    case BELOW_NORMAL_PRIORITY_CLASS:
        return GC_PRIORITY_BELOW_NORMAL;
    case NORMAL_PRIORITY_CLASS:
        return GC_PRIORITY_NORMAL;
    case ABOVE_NORMAL_PRIORITY_CLASS:
        return GC_PRIORITY_ABOVE_NORMAL;
    case HIGH_PRIORITY_CLASS:
        return GC_PRIORITY_HIGH;
    case REALTIME_PRIORITY_CLASS:
        return GC_PRIORITY_REALTIME;
    default:
        return GC_PRIORITY_UNKNOWN;
    }
}

static DWORD gc_priority_to_win32(gc_priority value)
{
    switch (value) {
    case GC_PRIORITY_IDLE:
        return IDLE_PRIORITY_CLASS;
    case GC_PRIORITY_BELOW_NORMAL:
        return BELOW_NORMAL_PRIORITY_CLASS;
    case GC_PRIORITY_NORMAL:
        return NORMAL_PRIORITY_CLASS;
    case GC_PRIORITY_ABOVE_NORMAL:
        return ABOVE_NORMAL_PRIORITY_CLASS;
    case GC_PRIORITY_HIGH:
        return HIGH_PRIORITY_CLASS;
    case GC_PRIORITY_REALTIME:
        return REALTIME_PRIORITY_CLASS;
    case GC_PRIORITY_UNKNOWN:
    default:
        return 0;
    }
}

static bool gc_win32_count_threads(DWORD pid, uint32_t *count, gc_win32_error *error)
{
    HANDLE snapshot;
    THREADENTRY32 entry;
    uint32_t result = 0;

    if (count == NULL) {
        gc_win32_set_error(error, "thread count", ERROR_INVALID_PARAMETER);
        return false;
    }

    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        gc_win32_set_error(error, "CreateToolhelp32Snapshot for threads", GetLastError());
        return false;
    }

    memset(&entry, 0, sizeof(entry));
    entry.dwSize = sizeof(entry);
    if (!Thread32First(snapshot, &entry)) {
        DWORD code = GetLastError();
        CloseHandle(snapshot);
        gc_win32_set_error(error, "Thread32First", code);
        return false;
    }

    do {
        if (entry.th32OwnerProcessID == pid) {
            ++result;
        }
    } while (Thread32Next(snapshot, &entry));

    if (GetLastError() != ERROR_NO_MORE_FILES) {
        DWORD code = GetLastError();
        CloseHandle(snapshot);
        gc_win32_set_error(error, "Thread32Next", code);
        return false;
    }

    CloseHandle(snapshot);
    *count = result;
    return true;
}

bool gc_win32_utf8_to_wide(
    const char *input,
    wchar_t *output,
    size_t output_capacity,
    gc_win32_error *error)
{
    int converted;

    if (input == NULL || output == NULL || output_capacity == 0 || output_capacity > INT_MAX) {
        gc_win32_set_error(error, "UTF-8 conversion", ERROR_INVALID_PARAMETER);
        return false;
    }

    converted = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        input,
        -1,
        output,
        (int)output_capacity);
    if (converted == 0) {
        gc_win32_set_error(error, "MultiByteToWideChar", GetLastError());
        return false;
    }
    return true;
}

bool gc_win32_find_processes(
    const wchar_t *executable_name,
    gc_process_candidates *candidates,
    gc_win32_error *error)
{
    HANDLE snapshot;
    PROCESSENTRY32W entry;

    if (executable_name == NULL || candidates == NULL) {
        gc_win32_set_error(error, "process discovery", ERROR_INVALID_PARAMETER);
        return false;
    }

    memset(candidates, 0, sizeof(*candidates));
    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        gc_win32_set_error(error, "CreateToolhelp32Snapshot for processes", GetLastError());
        return false;
    }

    memset(&entry, 0, sizeof(entry));
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot, &entry)) {
        DWORD code = GetLastError();
        CloseHandle(snapshot);
        gc_win32_set_error(error, "Process32FirstW", code);
        return false;
    }

    do {
        if (_wcsicmp(entry.szExeFile, executable_name) == 0) {
            if (candidates->stored_count < GC_PROCESS_CANDIDATE_CAPACITY) {
                gc_process_candidate *candidate = &candidates->items[candidates->stored_count++];
                candidate->pid = entry.th32ProcessID;
                candidate->thread_count = entry.cntThreads;
            }
            ++candidates->total_count;
        }
    } while (Process32NextW(snapshot, &entry));

    if (GetLastError() != ERROR_NO_MORE_FILES) {
        DWORD code = GetLastError();
        CloseHandle(snapshot);
        gc_win32_set_error(error, "Process32NextW", code);
        return false;
    }

    CloseHandle(snapshot);
    return true;
}

bool gc_win32_open_process(
    DWORD pid,
    bool allow_priority_change,
    gc_win32_process *process,
    gc_win32_error *error)
{
    DWORD access = PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE;
    char context[GC_WIN32_ERROR_CONTEXT_CAPACITY];

    if (process == NULL || pid == 0) {
        gc_win32_set_error(error, "OpenProcess", ERROR_INVALID_PARAMETER);
        return false;
    }

    memset(process, 0, sizeof(*process));
    if (allow_priority_change) {
        access |= PROCESS_SET_INFORMATION;
    }

    process->handle = OpenProcess(access, FALSE, pid);
    if (process->handle == NULL) {
        snprintf(context, sizeof(context), "OpenProcess for PID %lu", (unsigned long)pid);
        gc_win32_set_error(error, context, GetLastError());
        return false;
    }

    process->pid = pid;
    return true;
}

void gc_win32_close_process(gc_win32_process *process)
{
    if (process == NULL) {
        return;
    }

    if (process->handle != NULL && process->handle != INVALID_HANDLE_VALUE) {
        CloseHandle(process->handle);
    }
    process->handle = NULL;
    process->pid = 0;
}

bool gc_win32_read_metrics(
    const gc_win32_process *process,
    gc_metrics *metrics,
    gc_priority *priority,
    gc_win32_error *error)
{
    FILETIME creation_time;
    FILETIME exit_time;
    FILETIME kernel_time;
    FILETIME user_time;
    FILETIME sample_time;
    PROCESS_MEMORY_COUNTERS_EX memory;
    IO_COUNTERS io;
    MEMORYSTATUSEX system_memory;
    DWORD handle_count;
    DWORD priority_class;
    DWORD_PTR process_affinity;
    DWORD_PTR system_affinity;
    uint64_t sample_value;
    uint64_t creation_value;

    if (process == NULL || process->handle == NULL || metrics == NULL || priority == NULL) {
        gc_win32_set_error(error, "metric collection", ERROR_INVALID_PARAMETER);
        return false;
    }

    memset(metrics, 0, sizeof(*metrics));
    memset(&memory, 0, sizeof(memory));
    memory.cb = sizeof(memory);
    memset(&system_memory, 0, sizeof(system_memory));
    system_memory.dwLength = sizeof(system_memory);

    GetSystemTimeAsFileTime(&sample_time);
    if (!GetProcessTimes(process->handle, &creation_time, &exit_time, &kernel_time, &user_time)) {
        gc_win32_set_error(error, "GetProcessTimes", GetLastError());
        return false;
    }
    if (!GetProcessMemoryInfo(
            process->handle,
            (PROCESS_MEMORY_COUNTERS *)&memory,
            (DWORD)sizeof(memory))) {
        gc_win32_set_error(error, "GetProcessMemoryInfo", GetLastError());
        return false;
    }
    if (!GetProcessIoCounters(process->handle, &io)) {
        gc_win32_set_error(error, "GetProcessIoCounters", GetLastError());
        return false;
    }
    if (!GetProcessHandleCount(process->handle, &handle_count)) {
        gc_win32_set_error(error, "GetProcessHandleCount", GetLastError());
        return false;
    }
    if (!GetProcessAffinityMask(process->handle, &process_affinity, &system_affinity)) {
        gc_win32_set_error(error, "GetProcessAffinityMask", GetLastError());
        return false;
    }
    if (!GlobalMemoryStatusEx(&system_memory)) {
        gc_win32_set_error(error, "GlobalMemoryStatusEx", GetLastError());
        return false;
    }
    if (!gc_win32_count_threads(process->pid, &metrics->thread_count, error)) {
        return false;
    }

    priority_class = GetPriorityClass(process->handle);
    if (priority_class == 0) {
        gc_win32_set_error(error, "GetPriorityClass", GetLastError());
        return false;
    }

    *priority = gc_priority_from_win32(priority_class);
    if (*priority == GC_PRIORITY_UNKNOWN) {
        gc_win32_set_error(error, "GetPriorityClass returned an unsupported value", ERROR_INVALID_DATA);
        return false;
    }

    sample_value = gc_filetime_to_u64(sample_time);
    creation_value = gc_filetime_to_u64(creation_time);
    metrics->sample_time_100ns = sample_value;
    metrics->process_cpu_time_100ns =
        gc_filetime_to_u64(kernel_time) + gc_filetime_to_u64(user_time);
    metrics->uptime_ms = sample_value >= creation_value ? (sample_value - creation_value) / 10000U : 0;
    metrics->working_set_bytes = (uint64_t)memory.WorkingSetSize;
    metrics->private_bytes = (uint64_t)memory.PrivateUsage;
    metrics->available_memory_bytes = system_memory.ullAvailPhys;
    metrics->io_read_bytes = io.ReadTransferCount;
    metrics->io_write_bytes = io.WriteTransferCount;
    metrics->process_affinity_mask = (uint64_t)process_affinity;
    metrics->system_affinity_mask = (uint64_t)system_affinity;
    metrics->handle_count = handle_count;
    return true;
}

bool gc_win32_set_priority(
    const gc_win32_process *process,
    gc_priority priority,
    gc_win32_error *error)
{
    DWORD priority_class;

    if (process == NULL || process->handle == NULL) {
        gc_win32_set_error(error, "SetPriorityClass", ERROR_INVALID_PARAMETER);
        return false;
    }

    priority_class = gc_priority_to_win32(priority);
    if (priority_class == 0) {
        gc_win32_set_error(error, "SetPriorityClass", ERROR_INVALID_PARAMETER);
        return false;
    }

    if (!SetPriorityClass(process->handle, priority_class)) {
        gc_win32_set_error(error, "SetPriorityClass", GetLastError());
        return false;
    }
    return true;
}

bool gc_win32_is_process_active(
    const gc_win32_process *process,
    bool *active,
    gc_win32_error *error)
{
    DWORD exit_code;

    if (process == NULL || process->handle == NULL || active == NULL) {
        gc_win32_set_error(error, "GetExitCodeProcess", ERROR_INVALID_PARAMETER);
        return false;
    }

    if (!GetExitCodeProcess(process->handle, &exit_code)) {
        gc_win32_set_error(error, "GetExitCodeProcess", GetLastError());
        return false;
    }

    *active = exit_code == STILL_ACTIVE;
    return true;
}

DWORD gc_win32_wait(const gc_win32_process *process, DWORD timeout_ms)
{
    if (process == NULL || process->handle == NULL) {
        return WAIT_FAILED;
    }
    return WaitForSingleObject(process->handle, timeout_ms);
}

uint32_t gc_win32_logical_processor_count(void)
{
    SYSTEM_INFO info;
    GetNativeSystemInfo(&info);
    return info.dwNumberOfProcessors == 0 ? 1U : (uint32_t)info.dwNumberOfProcessors;
}

void gc_win32_make_session_id(char output[GC_SESSION_ID_CAPACITY])
{
    SYSTEMTIME now;
    LARGE_INTEGER counter;
    unsigned int suffix;

    if (output == NULL) {
        return;
    }

    GetLocalTime(&now);
    QueryPerformanceCounter(&counter);
    suffix = (unsigned int)((counter.QuadPart ^ GetCurrentProcessId()) & 0xFFFF);
    snprintf(
        output,
        GC_SESSION_ID_CAPACITY,
        "GC-%04u%02u%02u-%02u%02u%02u-%04X",
        (unsigned int)now.wYear,
        (unsigned int)now.wMonth,
        (unsigned int)now.wDay,
        (unsigned int)now.wHour,
        (unsigned int)now.wMinute,
        (unsigned int)now.wSecond,
        suffix);
}

const char *gc_win32_error_text(const gc_win32_error *error)
{
    static char formatted[GC_WIN32_ERROR_CONTEXT_CAPACITY + GC_WIN32_ERROR_MESSAGE_CAPACITY + 64];

    if (error == NULL) {
        return "unknown Win32 error";
    }

    snprintf(
        formatted,
        sizeof(formatted),
        "%s: %s (Win32 error %lu)",
        error->context[0] != '\0' ? error->context : "Win32 operation",
        error->message[0] != '\0' ? error->message : "unknown error",
        (unsigned long)error->code);
    return formatted;
}

#endif
