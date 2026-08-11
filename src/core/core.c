#include "ghostclock/core.h"

#include <string.h>

static bool gc_copy_string(char *destination, size_t capacity, const char *source)
{
    size_t length;

    if (destination == NULL || source == NULL || capacity == 0) {
        return false;
    }

    length = strlen(source);
    if (length >= capacity) {
        return false;
    }

    memcpy(destination, source, length + 1);
    return true;
}

static int gc_priority_rank(gc_priority priority)
{
    switch (priority) {
    case GC_PRIORITY_IDLE:
        return 1;
    case GC_PRIORITY_BELOW_NORMAL:
        return 2;
    case GC_PRIORITY_NORMAL:
        return 3;
    case GC_PRIORITY_ABOVE_NORMAL:
        return 4;
    case GC_PRIORITY_HIGH:
        return 5;
    case GC_PRIORITY_REALTIME:
        return 6;
    case GC_PRIORITY_UNKNOWN:
    default:
        return 0;
    }
}

const char *gc_result_name(gc_result result)
{
    switch (result) {
    case GC_OK:
        return "OK";
    case GC_ERROR_INVALID_ARGUMENT:
        return "INVALID_ARGUMENT";
    case GC_ERROR_INVALID_STATE:
        return "INVALID_STATE";
    case GC_ERROR_BUFFER_TOO_SMALL:
        return "BUFFER_TOO_SMALL";
    default:
        return "UNKNOWN";
    }
}

const char *gc_mode_name(gc_mode mode)
{
    return mode == GC_MODE_BOOST ? "boost" : "observe";
}

const char *gc_profile_name(gc_profile profile)
{
    switch (profile) {
    case GC_PROFILE_BALANCED:
        return "balanced";
    case GC_PROFILE_GAMING:
        return "gaming";
    case GC_PROFILE_DEVELOPMENT:
        return "development";
    default:
        return "unknown";
    }
}

const char *gc_priority_name(gc_priority priority)
{
    switch (priority) {
    case GC_PRIORITY_IDLE:
        return "Idle";
    case GC_PRIORITY_BELOW_NORMAL:
        return "Below Normal";
    case GC_PRIORITY_NORMAL:
        return "Normal";
    case GC_PRIORITY_ABOVE_NORMAL:
        return "Above Normal";
    case GC_PRIORITY_HIGH:
        return "High";
    case GC_PRIORITY_REALTIME:
        return "Realtime";
    case GC_PRIORITY_UNKNOWN:
    default:
        return "Unknown";
    }
}

const char *gc_session_state_name(gc_session_state state)
{
    switch (state) {
    case GC_SESSION_CREATED:
        return "CREATED";
    case GC_SESSION_SNAPSHOTTED:
        return "SNAPSHOTTED";
    case GC_SESSION_ACTIVE:
        return "ACTIVE";
    case GC_SESSION_ROLLING_BACK:
        return "ROLLING_BACK";
    case GC_SESSION_COMPLETED:
        return "COMPLETED";
    case GC_SESSION_FAILED:
        return "FAILED";
    default:
        return "UNKNOWN";
    }
}

const char *gc_rollback_status_name(gc_rollback_status status)
{
    switch (status) {
    case GC_ROLLBACK_NOT_REQUIRED:
        return "NOT_REQUIRED";
    case GC_ROLLBACK_PENDING:
        return "PENDING";
    case GC_ROLLBACK_RESTORED:
        return "RESTORED";
    case GC_ROLLBACK_TARGET_EXITED:
        return "TARGET_EXITED";
    case GC_ROLLBACK_FAILED:
        return "FAILED";
    default:
        return "UNKNOWN";
    }
}

gc_result gc_session_init(
    gc_session *session,
    const char *id,
    const char *target_name,
    uint32_t pid,
    gc_mode mode,
    gc_profile profile)
{
    if (session == NULL || id == NULL || target_name == NULL || pid == 0) {
        return GC_ERROR_INVALID_ARGUMENT;
    }

    memset(session, 0, sizeof(*session));
    if (!gc_copy_string(session->id, sizeof(session->id), id) ||
        !gc_copy_string(session->target_name, sizeof(session->target_name), target_name)) {
        return GC_ERROR_BUFFER_TOO_SMALL;
    }

    session->pid = pid;
    session->mode = mode;
    session->profile = profile;
    session->state = GC_SESSION_CREATED;
    session->rollback_status = GC_ROLLBACK_NOT_REQUIRED;
    return GC_OK;
}

gc_result gc_session_snapshot_priority(gc_session *session, gc_priority original_priority)
{
    if (session == NULL || original_priority == GC_PRIORITY_UNKNOWN) {
        return GC_ERROR_INVALID_ARGUMENT;
    }
    if (session->state != GC_SESSION_CREATED || session->priority_snapshot_valid) {
        return GC_ERROR_INVALID_STATE;
    }

    session->original_priority = original_priority;
    session->priority_snapshot_valid = true;
    session->state = GC_SESSION_SNAPSHOTTED;
    return GC_OK;
}

gc_result gc_session_plan_priority(gc_session *session, gc_priority planned_priority)
{
    if (session == NULL || planned_priority == GC_PRIORITY_UNKNOWN) {
        return GC_ERROR_INVALID_ARGUMENT;
    }
    if (session->state != GC_SESSION_SNAPSHOTTED || !session->priority_snapshot_valid) {
        return GC_ERROR_INVALID_STATE;
    }

    session->planned_priority = planned_priority;
    session->priority_change_planned =
        gc_priority_rank(session->original_priority) < gc_priority_rank(planned_priority);
    return GC_OK;
}

gc_result gc_session_mark_priority_applied(gc_session *session)
{
    if (session == NULL) {
        return GC_ERROR_INVALID_ARGUMENT;
    }
    if (session->state != GC_SESSION_SNAPSHOTTED || !session->priority_change_planned) {
        return GC_ERROR_INVALID_STATE;
    }

    session->priority_change_applied = true;
    session->rollback_status = GC_ROLLBACK_PENDING;
    return GC_OK;
}

gc_result gc_session_start(gc_session *session)
{
    if (session == NULL) {
        return GC_ERROR_INVALID_ARGUMENT;
    }
    if (session->state != GC_SESSION_CREATED && session->state != GC_SESSION_SNAPSHOTTED) {
        return GC_ERROR_INVALID_STATE;
    }

    session->state = GC_SESSION_ACTIVE;
    return GC_OK;
}

gc_result gc_session_begin_rollback(gc_session *session)
{
    if (session == NULL) {
        return GC_ERROR_INVALID_ARGUMENT;
    }
    if (session->state != GC_SESSION_ACTIVE || session->rollback_status != GC_ROLLBACK_PENDING) {
        return GC_ERROR_INVALID_STATE;
    }

    session->state = GC_SESSION_ROLLING_BACK;
    return GC_OK;
}

gc_result gc_session_mark_priority_restored(gc_session *session)
{
    if (session == NULL) {
        return GC_ERROR_INVALID_ARGUMENT;
    }
    if (session->state != GC_SESSION_ROLLING_BACK || session->rollback_status != GC_ROLLBACK_PENDING) {
        return GC_ERROR_INVALID_STATE;
    }

    session->priority_change_applied = false;
    session->rollback_status = GC_ROLLBACK_RESTORED;
    return GC_OK;
}

gc_result gc_session_mark_target_exited(gc_session *session)
{
    if (session == NULL) {
        return GC_ERROR_INVALID_ARGUMENT;
    }
    if (session->state != GC_SESSION_ACTIVE) {
        return GC_ERROR_INVALID_STATE;
    }

    if (session->rollback_status == GC_ROLLBACK_PENDING) {
        session->priority_change_applied = false;
        session->rollback_status = GC_ROLLBACK_TARGET_EXITED;
    }
    return GC_OK;
}

gc_result gc_session_mark_rollback_failed(gc_session *session)
{
    if (session == NULL) {
        return GC_ERROR_INVALID_ARGUMENT;
    }
    if (session->state != GC_SESSION_ROLLING_BACK || session->rollback_status != GC_ROLLBACK_PENDING) {
        return GC_ERROR_INVALID_STATE;
    }

    session->rollback_status = GC_ROLLBACK_FAILED;
    session->state = GC_SESSION_FAILED;
    return GC_OK;
}

gc_result gc_session_complete(gc_session *session)
{
    if (session == NULL) {
        return GC_ERROR_INVALID_ARGUMENT;
    }
    if (session->state == GC_SESSION_COMPLETED || session->state == GC_SESSION_FAILED ||
        session->rollback_status == GC_ROLLBACK_PENDING || session->rollback_status == GC_ROLLBACK_FAILED) {
        return GC_ERROR_INVALID_STATE;
    }

    session->state = GC_SESSION_COMPLETED;
    return GC_OK;
}

gc_result gc_session_fail(gc_session *session)
{
    if (session == NULL) {
        return GC_ERROR_INVALID_ARGUMENT;
    }
    if (session->state == GC_SESSION_COMPLETED) {
        return GC_ERROR_INVALID_STATE;
    }

    session->state = GC_SESSION_FAILED;
    return GC_OK;
}

double gc_metrics_cpu_percent(
    const gc_metrics *before,
    const gc_metrics *after,
    uint32_t logical_processor_count)
{
    uint64_t process_delta;
    uint64_t wall_delta;
    double percent;

    if (before == NULL || after == NULL || logical_processor_count == 0 ||
        after->sample_time_100ns <= before->sample_time_100ns ||
        after->process_cpu_time_100ns < before->process_cpu_time_100ns) {
        return 0.0;
    }

    process_delta = after->process_cpu_time_100ns - before->process_cpu_time_100ns;
    wall_delta = after->sample_time_100ns - before->sample_time_100ns;
    percent = ((double)process_delta / ((double)wall_delta * logical_processor_count)) * 100.0;

    if (percent < 0.0) {
        return 0.0;
    }
    if (percent > 100.0) {
        return 100.0;
    }
    return percent;
}
