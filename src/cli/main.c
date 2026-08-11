#include "ghostclock/core.h"
#include "ghostclock/windows.h"

#ifdef _WIN32

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GC_VERSION "0.1.0"
#define GC_SAMPLE_INTERVAL_MS 1000U
#define GC_BASELINE_INTERVAL_MS 500U

typedef struct gc_cli_options {
    gc_mode mode;
    gc_profile profile;
    const char *target_name;
    DWORD selected_pid;
    bool dry_run;
} gc_cli_options;

typedef enum gc_monitor_result {
    GC_MONITOR_TARGET_EXITED = 0,
    GC_MONITOR_INTERRUPTED,
    GC_MONITOR_ERROR
} gc_monitor_result;

static volatile LONG gc_stop_requested = 0;

static BOOL WINAPI gc_console_handler(DWORD signal)
{
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT || signal == CTRL_CLOSE_EVENT) {
        InterlockedExchange(&gc_stop_requested, 1);
        return TRUE;
    }
    return FALSE;
}

static void gc_print_usage(FILE *stream)
{
    fprintf(stream, "GhostClock %s\n\n", GC_VERSION);
    fprintf(stream, "Usage:\n");
    fprintf(stream, "  ghostclock observe <process.exe> [--pid <pid>]\n");
    fprintf(stream, "  ghostclock boost <process.exe> [--profile <name>] [--pid <pid>] [--dry-run]\n");
    fprintf(stream, "  ghostclock --version\n\n");
    fprintf(stream, "Profiles: balanced, gaming, development\n");
}

static bool gc_parse_pid(const char *value, DWORD *pid)
{
    char *end = NULL;
    unsigned long parsed;

    if (value == NULL || pid == NULL || value[0] == '\0') {
        return false;
    }

    errno = 0;
    parsed = strtoul(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || parsed == 0 || parsed > UINT32_MAX) {
        return false;
    }

    *pid = (DWORD)parsed;
    return true;
}

static bool gc_parse_options(int argc, char **argv, gc_cli_options *options)
{
    int index;

    if (options == NULL || argc < 3) {
        return false;
    }

    memset(options, 0, sizeof(*options));
    options->profile = GC_PROFILE_BALANCED;
    options->target_name = argv[2];

    if (strcmp(argv[1], "observe") == 0) {
        options->mode = GC_MODE_OBSERVE;
    } else if (strcmp(argv[1], "boost") == 0) {
        options->mode = GC_MODE_BOOST;
    } else {
        return false;
    }

    for (index = 3; index < argc; ++index) {
        if (strcmp(argv[index], "--dry-run") == 0) {
            if (options->mode != GC_MODE_BOOST) {
                return false;
            }
            options->dry_run = true;
        } else if (strcmp(argv[index], "--profile") == 0) {
            if (options->mode != GC_MODE_BOOST || index + 1 >= argc ||
                gc_profile_parse(argv[++index], &options->profile) != GC_OK) {
                return false;
            }
        } else if (strcmp(argv[index], "--pid") == 0) {
            if (index + 1 >= argc || !gc_parse_pid(argv[++index], &options->selected_pid)) {
                return false;
            }
        } else {
            return false;
        }
    }

    return true;
}

static bool gc_select_process(
    const gc_process_candidates *candidates,
    DWORD selected_pid,
    DWORD *pid)
{
    size_t index;

    if (candidates == NULL || pid == NULL) {
        return false;
    }

    if (selected_pid != 0) {
        for (index = 0; index < candidates->stored_count; ++index) {
            if (candidates->items[index].pid == selected_pid) {
                *pid = selected_pid;
                return true;
            }
        }
        fprintf(stderr, "[ERROR] PID %lu does not match the requested executable\n", (unsigned long)selected_pid);
        return false;
    }

    if (candidates->total_count == 0) {
        return false;
    }
    if (candidates->total_count > 1) {
        fprintf(stderr, "[ERROR] multiple matching processes found; select one with --pid\n");
        for (index = 0; index < candidates->stored_count; ++index) {
            fprintf(stderr, "[INFO] candidate pid=%lu threads=%lu\n",
                    (unsigned long)candidates->items[index].pid,
                    (unsigned long)candidates->items[index].thread_count);
        }
        if (candidates->total_count > candidates->stored_count) {
            fprintf(stderr, "[WARN] additional candidates omitted count=%zu\n",
                    candidates->total_count - candidates->stored_count);
        }
        return false;
    }

    *pid = candidates->items[0].pid;
    return true;
}

static void gc_print_metrics(const char *heading, const gc_metrics *metrics, double cpu_percent)
{
    const double bytes_per_mib = 1024.0 * 1024.0;

    printf("\n%s\n", heading);
    printf("CPU: %.2f%%\n", cpu_percent);
    printf("Working set: %.2f MiB\n", metrics->working_set_bytes / bytes_per_mib);
    printf("Private memory: %.2f MiB\n", metrics->private_bytes / bytes_per_mib);
    printf("Available memory: %.2f MiB\n", metrics->available_memory_bytes / bytes_per_mib);
    printf("Threads: %lu\n", (unsigned long)metrics->thread_count);
    printf("Handles: %lu\n", (unsigned long)metrics->handle_count);
    printf("I/O read: %llu bytes\n", (unsigned long long)metrics->io_read_bytes);
    printf("I/O write: %llu bytes\n", (unsigned long long)metrics->io_write_bytes);
    printf("Uptime: %.2f seconds\n", metrics->uptime_ms / 1000.0);
}

static bool gc_collect_baseline(
    const gc_win32_process *process,
    gc_metrics *baseline,
    gc_priority *priority,
    double *cpu_percent,
    gc_win32_error *error)
{
    gc_metrics first;
    gc_priority first_priority;
    DWORD wait_result;

    if (!gc_win32_read_metrics(process, &first, &first_priority, error)) {
        return false;
    }

    wait_result = gc_win32_wait(process, GC_BASELINE_INTERVAL_MS);
    if (wait_result == WAIT_OBJECT_0) {
        fprintf(stderr, "[ERROR] target exited during baseline collection\n");
        return false;
    }
    if (wait_result == WAIT_FAILED) {
        fprintf(stderr, "[ERROR] WaitForSingleObject failed during baseline collection: Win32 error %lu\n",
                (unsigned long)GetLastError());
        return false;
    }

    if (!gc_win32_read_metrics(process, baseline, priority, error)) {
        return false;
    }

    *cpu_percent = gc_metrics_cpu_percent(&first, baseline, gc_win32_logical_processor_count());
    return true;
}

static gc_monitor_result gc_monitor_process(
    const gc_win32_process *process,
    gc_metrics *previous,
    gc_metrics *last,
    gc_win32_error *error)
{
    DWORD wait_result;
    gc_priority current_priority;
    double cpu_percent;

    for (;;) {
        if (InterlockedCompareExchange(&gc_stop_requested, 0, 0) != 0) {
            return GC_MONITOR_INTERRUPTED;
        }

        wait_result = gc_win32_wait(process, GC_SAMPLE_INTERVAL_MS);
        if (wait_result == WAIT_OBJECT_0) {
            return GC_MONITOR_TARGET_EXITED;
        }
        if (wait_result == WAIT_FAILED) {
            fprintf(stderr, "[ERROR] WaitForSingleObject failed while monitoring: Win32 error %lu\n",
                    (unsigned long)GetLastError());
            return GC_MONITOR_ERROR;
        }

        if (!gc_win32_read_metrics(process, last, &current_priority, error)) {
            bool active;
            if (gc_win32_is_process_active(process, &active, error) && !active) {
                return GC_MONITOR_TARGET_EXITED;
            }
            return GC_MONITOR_ERROR;
        }

        cpu_percent = gc_metrics_cpu_percent(previous, last, gc_win32_logical_processor_count());
        printf("[INFO] metrics cpu=%.2f%% working_set_mib=%.2f threads=%lu io_read=%llu io_write=%llu\n",
               cpu_percent,
               last->working_set_bytes / (1024.0 * 1024.0),
               (unsigned long)last->thread_count,
               (unsigned long long)last->io_read_bytes,
               (unsigned long long)last->io_write_bytes);
        *previous = *last;
    }
}

static int gc_restore_if_needed(
    gc_session *session,
    const gc_win32_process *process,
    gc_win32_error *error)
{
    bool active;

    if (session->rollback_status != GC_ROLLBACK_PENDING) {
        return 0;
    }

    if (!gc_win32_is_process_active(process, &active, error)) {
        fprintf(stderr, "[ERROR] %s\n", gc_win32_error_text(error));
        gc_session_fail(session);
        return 5;
    }
    if (!active) {
        gc_session_mark_target_exited(session);
        return 0;
    }

    if (gc_session_begin_rollback(session) != GC_OK) {
        fprintf(stderr, "[FATAL] rollback state transition failed\n");
        gc_session_fail(session);
        return 5;
    }
    if (!gc_win32_set_priority(process, session->original_priority, error)) {
        fprintf(stderr, "[FATAL] rollback failed: %s\n", gc_win32_error_text(error));
        gc_session_mark_rollback_failed(session);
        return 5;
    }

    gc_session_mark_priority_restored(session);
    printf("[INFO] rollback completed priority=%s\n", gc_priority_name(session->original_priority));
    return 0;
}

static void gc_print_session_report(const gc_session *session)
{
    printf("\nSession report\n");
    printf("ID: %s\n", session->id);
    printf("State: %s\n", gc_session_state_name(session->state));
    printf("Rollback: %s\n", gc_rollback_status_name(session->rollback_status));
}

int main(int argc, char **argv)
{
    gc_cli_options options;
    gc_process_candidates candidates;
    gc_win32_process process;
    gc_win32_error error;
    gc_session session;
    gc_metrics baseline;
    gc_metrics previous;
    gc_metrics last;
    gc_priority original_priority;
    gc_priority planned_priority;
    gc_monitor_result monitor_result;
    wchar_t target_name_wide[GC_TARGET_NAME_CAPACITY];
    char session_id[GC_SESSION_ID_CAPACITY];
    DWORD pid = 0;
    double baseline_cpu = 0.0;
    int exit_code = 0;
    bool session_initialized = false;
    bool process_open = false;

    memset(&process, 0, sizeof(process));
    memset(&error, 0, sizeof(error));
    memset(&session, 0, sizeof(session));

    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("GhostClock %s\n", GC_VERSION);
        return 0;
    }
    if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        gc_print_usage(stdout);
        return 0;
    }
    if (!gc_parse_options(argc, argv, &options)) {
        gc_print_usage(stderr);
        return 2;
    }

    if (!gc_win32_utf8_to_wide(
            options.target_name,
            target_name_wide,
            GC_TARGET_NAME_CAPACITY,
            &error)) {
        fprintf(stderr, "[ERROR] %s\n", gc_win32_error_text(&error));
        return 3;
    }
    if (!gc_win32_find_processes(target_name_wide, &candidates, &error)) {
        fprintf(stderr, "[ERROR] %s\n", gc_win32_error_text(&error));
        return 3;
    }
    if (candidates.total_count == 0) {
        fprintf(stderr, "[ERROR] target not found process=%s\n", options.target_name);
        return 3;
    }
    if (!gc_select_process(&candidates, options.selected_pid, &pid)) {
        return 3;
    }

    if (!gc_win32_open_process(
            pid,
            options.mode == GC_MODE_BOOST && !options.dry_run,
            &process,
            &error)) {
        fprintf(stderr, "[ERROR] %s\n", gc_win32_error_text(&error));
        return 4;
    }
    process_open = true;

    if (!gc_collect_baseline(&process, &baseline, &original_priority, &baseline_cpu, &error)) {
        if (error.code != 0) {
            fprintf(stderr, "[ERROR] %s\n", gc_win32_error_text(&error));
        }
        exit_code = 4;
        goto cleanup;
    }

    gc_win32_make_session_id(session_id);
    if (gc_session_init(
            &session,
            session_id,
            options.target_name,
            (uint32_t)pid,
            options.mode,
            options.profile) != GC_OK) {
        fprintf(stderr, "[FATAL] session initialization failed\n");
        exit_code = 4;
        goto cleanup;
    }
    session_initialized = true;

    printf("GhostClock %s\n\n", GC_VERSION);
    printf("Target: %s\n", options.target_name);
    printf("PID: %lu\n", (unsigned long)pid);
    printf("Profile: %s\n", gc_profile_name(options.profile));
    printf("Mode: %s%s\n", gc_mode_name(options.mode), options.dry_run ? " (dry-run)" : "");
    gc_print_metrics("Baseline", &baseline, baseline_cpu);

    previous = baseline;
    last = baseline;

    if (options.mode == GC_MODE_BOOST) {
        if (gc_session_snapshot_priority(&session, original_priority) != GC_OK) {
            fprintf(stderr, "[FATAL] priority snapshot failed\n");
            exit_code = 4;
            goto cleanup;
        }

        planned_priority = gc_profile_target_priority(options.profile);
        if (gc_session_plan_priority(&session, planned_priority) != GC_OK) {
            fprintf(stderr, "[FATAL] priority plan failed\n");
            exit_code = 4;
            goto cleanup;
        }

        printf("\nChanges\n");
        if (session.priority_change_planned) {
            printf("Process priority: %s -> %s\n",
                   gc_priority_name(original_priority),
                   gc_priority_name(planned_priority));
        } else {
            printf("Process priority: unchanged reason=current_priority_is_sufficient\n");
        }

        if (options.dry_run) {
            printf("\n[INFO] dry-run completed no_changes_applied=true\n");
            gc_session_complete(&session);
            gc_print_session_report(&session);
            goto cleanup;
        }

        if (session.priority_change_planned) {
            if (!gc_win32_set_priority(&process, planned_priority, &error)) {
                fprintf(stderr, "[ERROR] %s\n", gc_win32_error_text(&error));
                gc_session_fail(&session);
                exit_code = 4;
                goto cleanup;
            }
            gc_session_mark_priority_applied(&session);
            printf("[INFO] priority changed from=%s to=%s\n",
                   gc_priority_name(original_priority),
                   gc_priority_name(planned_priority));
        }
    }

    if (gc_session_start(&session) != GC_OK) {
        fprintf(stderr, "[FATAL] session start failed\n");
        exit_code = 4;
        goto cleanup;
    }

    if (!SetConsoleCtrlHandler(gc_console_handler, TRUE)) {
        fprintf(stderr, "[ERROR] SetConsoleCtrlHandler failed: Win32 error %lu\n",
                (unsigned long)GetLastError());
        exit_code = gc_restore_if_needed(&session, &process, &error);
        if (exit_code == 0) {
            gc_session_fail(&session);
            exit_code = 4;
        }
        goto cleanup;
    }

    printf("\n[INFO] monitoring session=%s interval_ms=%u\n", session.id, GC_SAMPLE_INTERVAL_MS);
    monitor_result = gc_monitor_process(&process, &previous, &last, &error);

    if (monitor_result == GC_MONITOR_TARGET_EXITED) {
        gc_session_mark_target_exited(&session);
        printf("[INFO] target exited pid=%lu\n", (unsigned long)pid);
    } else {
        if (monitor_result == GC_MONITOR_ERROR && error.code != 0) {
            fprintf(stderr, "[ERROR] %s\n", gc_win32_error_text(&error));
        } else if (monitor_result == GC_MONITOR_INTERRUPTED) {
            printf("[INFO] stop requested signal=console_control\n");
        }

        exit_code = gc_restore_if_needed(&session, &process, &error);
        if (monitor_result == GC_MONITOR_ERROR && exit_code == 0) {
            gc_session_fail(&session);
            exit_code = 4;
        }
    }

    SetConsoleCtrlHandler(gc_console_handler, FALSE);

    if (exit_code == 0 && session.state != GC_SESSION_FAILED) {
        if (gc_session_complete(&session) != GC_OK) {
            fprintf(stderr, "[FATAL] session completion failed rollback=%s\n",
                    gc_rollback_status_name(session.rollback_status));
            gc_session_fail(&session);
            exit_code = 5;
        }
    }

    gc_print_session_report(&session);

cleanup:
    if (session_initialized && session.rollback_status == GC_ROLLBACK_PENDING && process_open) {
        int rollback_exit = gc_restore_if_needed(&session, &process, &error);
        if (rollback_exit != 0) {
            exit_code = rollback_exit;
        }
    }
    if (process_open) {
        gc_win32_close_process(&process);
    }
    return exit_code;
}

#endif
