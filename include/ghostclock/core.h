#ifndef GHOSTCLOCK_CORE_H
#define GHOSTCLOCK_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GC_SESSION_ID_CAPACITY 32
#define GC_TARGET_NAME_CAPACITY 260

typedef enum gc_result {
    GC_OK = 0,
    GC_ERROR_INVALID_ARGUMENT,
    GC_ERROR_INVALID_STATE,
    GC_ERROR_BUFFER_TOO_SMALL
} gc_result;

typedef enum gc_mode {
    GC_MODE_OBSERVE = 0,
    GC_MODE_BOOST
} gc_mode;

typedef enum gc_profile {
    GC_PROFILE_BALANCED = 0,
    GC_PROFILE_GAMING,
    GC_PROFILE_DEVELOPMENT
} gc_profile;

typedef enum gc_priority {
    GC_PRIORITY_UNKNOWN = 0,
    GC_PRIORITY_IDLE,
    GC_PRIORITY_BELOW_NORMAL,
    GC_PRIORITY_NORMAL,
    GC_PRIORITY_ABOVE_NORMAL,
    GC_PRIORITY_HIGH,
    GC_PRIORITY_REALTIME
} gc_priority;

typedef enum gc_session_state {
    GC_SESSION_CREATED = 0,
    GC_SESSION_SNAPSHOTTED,
    GC_SESSION_ACTIVE,
    GC_SESSION_ROLLING_BACK,
    GC_SESSION_COMPLETED,
    GC_SESSION_FAILED
} gc_session_state;

typedef enum gc_rollback_status {
    GC_ROLLBACK_NOT_REQUIRED = 0,
    GC_ROLLBACK_PENDING,
    GC_ROLLBACK_RESTORED,
    GC_ROLLBACK_TARGET_EXITED,
    GC_ROLLBACK_FAILED
} gc_rollback_status;

typedef struct gc_metrics {
    uint64_t sample_time_100ns;
    uint64_t process_cpu_time_100ns;
    uint64_t uptime_ms;
    uint64_t working_set_bytes;
    uint64_t private_bytes;
    uint64_t available_memory_bytes;
    uint64_t io_read_bytes;
    uint64_t io_write_bytes;
    uint64_t process_affinity_mask;
    uint64_t system_affinity_mask;
    uint32_t thread_count;
    uint32_t handle_count;
} gc_metrics;

typedef struct gc_session {
    char id[GC_SESSION_ID_CAPACITY];
    char target_name[GC_TARGET_NAME_CAPACITY];
    uint32_t pid;
    gc_mode mode;
    gc_profile profile;
    gc_session_state state;
    gc_priority original_priority;
    gc_priority planned_priority;
    gc_rollback_status rollback_status;
    bool priority_snapshot_valid;
    bool priority_change_planned;
    bool priority_change_applied;
} gc_session;

const char *gc_result_name(gc_result result);
const char *gc_mode_name(gc_mode mode);
const char *gc_profile_name(gc_profile profile);
const char *gc_priority_name(gc_priority priority);
const char *gc_session_state_name(gc_session_state state);
const char *gc_rollback_status_name(gc_rollback_status status);

gc_result gc_profile_parse(const char *value, gc_profile *profile);
gc_priority gc_profile_target_priority(gc_profile profile);

gc_result gc_session_init(
    gc_session *session,
    const char *id,
    const char *target_name,
    uint32_t pid,
    gc_mode mode,
    gc_profile profile);
gc_result gc_session_snapshot_priority(gc_session *session, gc_priority original_priority);
gc_result gc_session_plan_priority(gc_session *session, gc_priority planned_priority);
gc_result gc_session_mark_priority_applied(gc_session *session);
gc_result gc_session_start(gc_session *session);
gc_result gc_session_begin_rollback(gc_session *session);
gc_result gc_session_mark_priority_restored(gc_session *session);
gc_result gc_session_mark_target_exited(gc_session *session);
gc_result gc_session_mark_rollback_failed(gc_session *session);
gc_result gc_session_complete(gc_session *session);
gc_result gc_session_fail(gc_session *session);

double gc_metrics_cpu_percent(
    const gc_metrics *before,
    const gc_metrics *after,
    uint32_t logical_processor_count);

#endif
