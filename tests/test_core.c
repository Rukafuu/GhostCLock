#include "ghostclock/core.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define GC_ASSERT(condition)                                                                      \
    do {                                                                                          \
        if (!(condition)) {                                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                 \
            ++failures;                                                                           \
        }                                                                                         \
    } while (0)

static void test_profile_parsing(void)
{
    gc_profile profile = GC_PROFILE_BALANCED;

    GC_ASSERT(gc_profile_parse("gaming", &profile) == GC_OK);
    GC_ASSERT(profile == GC_PROFILE_GAMING);
    GC_ASSERT(gc_profile_target_priority(profile) == GC_PRIORITY_HIGH);
    GC_ASSERT(gc_profile_parse("development", &profile) == GC_OK);
    GC_ASSERT(gc_profile_target_priority(profile) == GC_PRIORITY_ABOVE_NORMAL);
    GC_ASSERT(gc_profile_parse("unsupported", &profile) == GC_ERROR_INVALID_ARGUMENT);
}

static void test_session_lifecycle_with_rollback(void)
{
    gc_session session;

    GC_ASSERT(gc_session_init(
                  &session,
                  "GC-20260811-194233-A7F2",
                  "notepad.exe",
                  14320,
                  GC_MODE_BOOST,
                  GC_PROFILE_GAMING) == GC_OK);
    GC_ASSERT(session.state == GC_SESSION_CREATED);
    GC_ASSERT(gc_session_snapshot_priority(&session, GC_PRIORITY_NORMAL) == GC_OK);
    GC_ASSERT(gc_session_plan_priority(&session, GC_PRIORITY_HIGH) == GC_OK);
    GC_ASSERT(session.priority_change_planned);
    GC_ASSERT(gc_session_mark_priority_applied(&session) == GC_OK);
    GC_ASSERT(session.rollback_status == GC_ROLLBACK_PENDING);
    GC_ASSERT(gc_session_start(&session) == GC_OK);
    GC_ASSERT(gc_session_begin_rollback(&session) == GC_OK);
    GC_ASSERT(gc_session_mark_priority_restored(&session) == GC_OK);
    GC_ASSERT(gc_session_complete(&session) == GC_OK);
    GC_ASSERT(session.state == GC_SESSION_COMPLETED);
    GC_ASSERT(session.rollback_status == GC_ROLLBACK_RESTORED);
}

static void test_no_priority_downgrade(void)
{
    gc_session session;

    GC_ASSERT(gc_session_init(
                  &session,
                  "GC-20260811-194233-A7F3",
                  "build.exe",
                  2200,
                  GC_MODE_BOOST,
                  GC_PROFILE_DEVELOPMENT) == GC_OK);
    GC_ASSERT(gc_session_snapshot_priority(&session, GC_PRIORITY_HIGH) == GC_OK);
    GC_ASSERT(gc_session_plan_priority(&session, GC_PRIORITY_ABOVE_NORMAL) == GC_OK);
    GC_ASSERT(!session.priority_change_planned);
    GC_ASSERT(gc_session_mark_priority_applied(&session) == GC_ERROR_INVALID_STATE);
    GC_ASSERT(gc_session_start(&session) == GC_OK);
    GC_ASSERT(gc_session_complete(&session) == GC_OK);
    GC_ASSERT(session.rollback_status == GC_ROLLBACK_NOT_REQUIRED);
}

static void test_target_exit_resolves_pending_rollback(void)
{
    gc_session session;

    GC_ASSERT(gc_session_init(
                  &session,
                  "GC-20260811-194233-A7F4",
                  "game.exe",
                  3300,
                  GC_MODE_BOOST,
                  GC_PROFILE_GAMING) == GC_OK);
    GC_ASSERT(gc_session_snapshot_priority(&session, GC_PRIORITY_NORMAL) == GC_OK);
    GC_ASSERT(gc_session_plan_priority(&session, GC_PRIORITY_HIGH) == GC_OK);
    GC_ASSERT(gc_session_mark_priority_applied(&session) == GC_OK);
    GC_ASSERT(gc_session_start(&session) == GC_OK);
    GC_ASSERT(gc_session_mark_target_exited(&session) == GC_OK);
    GC_ASSERT(session.rollback_status == GC_ROLLBACK_TARGET_EXITED);
    GC_ASSERT(!session.priority_change_applied);
    GC_ASSERT(gc_session_complete(&session) == GC_OK);
}

static void test_invalid_transition_is_rejected(void)
{
    gc_session session;

    GC_ASSERT(gc_session_init(
                  &session,
                  "GC-20260811-194233-A7F5",
                  "app.exe",
                  4400,
                  GC_MODE_BOOST,
                  GC_PROFILE_BALANCED) == GC_OK);
    GC_ASSERT(gc_session_begin_rollback(&session) == GC_ERROR_INVALID_STATE);
    GC_ASSERT(gc_session_snapshot_priority(&session, GC_PRIORITY_UNKNOWN) == GC_ERROR_INVALID_ARGUMENT);
    GC_ASSERT(session.state == GC_SESSION_CREATED);
}

static void test_cpu_metric_calculation(void)
{
    gc_metrics before;
    gc_metrics after;
    double percent;

    memset(&before, 0, sizeof(before));
    memset(&after, 0, sizeof(after));
    before.sample_time_100ns = 10000000;
    after.sample_time_100ns = 20000000;
    before.process_cpu_time_100ns = 2000000;
    after.process_cpu_time_100ns = 6000000;

    percent = gc_metrics_cpu_percent(&before, &after, 4);
    GC_ASSERT(fabs(percent - 10.0) < 0.0001);
    GC_ASSERT(gc_metrics_cpu_percent(&after, &before, 4) == 0.0);
    GC_ASSERT(gc_metrics_cpu_percent(&before, &after, 0) == 0.0);
}

int main(void)
{
    test_profile_parsing();
    test_session_lifecycle_with_rollback();
    test_no_priority_downgrade();
    test_target_exit_resolves_pending_rollback();
    test_invalid_transition_is_rejected();
    test_cpu_metric_calculation();

    if (failures != 0) {
        fprintf(stderr, "%d test assertion(s) failed\n", failures);
        return 1;
    }

    printf("All core tests passed\n");
    return 0;
}
