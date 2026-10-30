/**
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 *
 * Test-only pthread implementation of the FreeRTOS calls used by can_rtos.c.
 */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <time.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

__thread int fake_in_isr = 0;

/* ---- helpers ---- */

static void deadline(struct timespec *ts, TickType_t ms)
{
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec  += ms / 1000U;
    ts->tv_nsec += (long)(ms % 1000U) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) { ts->tv_sec++; ts->tv_nsec -= 1000000000L; }
}

/* ---- semaphores and mutexes ---- */

typedef struct
{
    pthread_mutex_t m;
    pthread_cond_t  c;
    int             count;      /* Binary semaphore: 0 or 1. */
    bool            recursive;  /* Mutex with owner and depth. */
    pthread_t       owner;
    int             depth;
} FakeSem_t;

static FakeSem_t *sem_new(bool recursive)
{
    FakeSem_t *s = calloc(1, sizeof(*s));
    pthread_mutex_init(&s->m, NULL);
    pthread_cond_init(&s->c, NULL);
    s->recursive = recursive;
    return s;
}

SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void) { return sem_new(true); }
SemaphoreHandle_t xSemaphoreCreateBinary(void)         { return sem_new(false); }
void vSemaphoreDelete(SemaphoreHandle_t h)
{
    FakeSem_t *s = h;
    pthread_cond_destroy(&s->c);
    pthread_mutex_destroy(&s->m);
    free(s);
}

static void check_deleted(void);

BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t h, TickType_t ticks)
{
    FakeSem_t *s = h;
    struct timespec ts;
    BaseType_t ok = pdTRUE;

    check_deleted();
    deadline(&ts, ticks);
    pthread_mutex_lock(&s->m);
    if ((s->depth > 0) && pthread_equal(s->owner, pthread_self()))
    {
        s->depth++;
    }
    else
    {
        while (s->depth > 0)
        {
            int e = (ticks == portMAX_DELAY) ? pthread_cond_wait(&s->c, &s->m)
                                             : pthread_cond_timedwait(&s->c, &s->m, &ts);
            if (e == ETIMEDOUT) { ok = pdFALSE; break; }
        }
        if (ok == pdTRUE) { s->owner = pthread_self(); s->depth = 1; }
    }
    pthread_mutex_unlock(&s->m);
    return ok;
}

BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t h)
{
    FakeSem_t *s = h;
    BaseType_t ok = pdFALSE;

    pthread_mutex_lock(&s->m);
    if ((s->depth > 0) && pthread_equal(s->owner, pthread_self()))
    {
        s->depth--;
        ok = pdTRUE;
        if (s->depth == 0) { pthread_cond_broadcast(&s->c); }
    }
    pthread_mutex_unlock(&s->m);
    return ok;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t h, TickType_t ticks)
{
    FakeSem_t *s = h;
    struct timespec ts;
    BaseType_t ok = pdTRUE;

    check_deleted();
    deadline(&ts, ticks);
    pthread_mutex_lock(&s->m);
    while (s->count == 0)
    {
        int e;
        if (ticks == 0U) { ok = pdFALSE; break; }
        e = (ticks == portMAX_DELAY) ? pthread_cond_wait(&s->c, &s->m)
                                     : pthread_cond_timedwait(&s->c, &s->m, &ts);
        if (e == ETIMEDOUT) { ok = (s->count > 0) ? pdTRUE : pdFALSE; break; }
    }
    if ((ok == pdTRUE) && (s->count > 0)) { s->count--; }
    pthread_mutex_unlock(&s->m);
    return ok;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t h)
{
    FakeSem_t *s = h;
    pthread_mutex_lock(&s->m);
    if (s->count < 1) { s->count = 1; }                 /* Binary semaphore. */
    pthread_cond_broadcast(&s->c);
    pthread_mutex_unlock(&s->m);
    return pdTRUE;
}

BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t h, BaseType_t *woken)
{
    if (woken != NULL) { *woken = pdFALSE; }
    return xSemaphoreGive(h);
}

/* ---- tasks ---- */

typedef struct
{
    pthread_t       th;
    pthread_mutex_t m;
    pthread_cond_t  c;
    uint32_t        notify;
    bool            deleted;      /* Accessed with __atomic builtins. */
    TaskFunction_t  fn;
    void           *arg;
} FakeTask_t;

static __thread FakeTask_t *cur_task = NULL;
static volatile int g_task_count = 0;

static void check_deleted(void)
{
    if ((cur_task != NULL) && __atomic_load_n(&cur_task->deleted, __ATOMIC_ACQUIRE))
    {
        pthread_exit(NULL);                             /* The task was deleted: stop here, holding nothing. */
    }
}

static void *task_main(void *p)
{
    FakeTask_t *t = p;
    cur_task = t;
    t->fn(t->arg);
    return NULL;
}

BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, configSTACK_DEPTH_TYPE depth, void *arg, UBaseType_t prio, TaskHandle_t *out)
{
    FakeTask_t *t = calloc(1, sizeof(*t));
    (void)name; (void)depth; (void)prio;
    pthread_mutex_init(&t->m, NULL);
    pthread_cond_init(&t->c, NULL);
    t->fn = fn;
    t->arg = arg;
    if (pthread_create(&t->th, NULL, task_main, t) != 0) { free(t); return pdFALSE; }
    __atomic_add_fetch(&g_task_count, 1, __ATOMIC_SEQ_CST);
    *out = t;
    return pdPASS;
}

void vTaskDelete(TaskHandle_t h)
{
    FakeTask_t *t = h;
    __atomic_store_n(&t->deleted, true, __ATOMIC_RELEASE);
    pthread_mutex_lock(&t->m);
    pthread_cond_broadcast(&t->c);
    pthread_mutex_unlock(&t->m);
    pthread_join(t->th, NULL);
    __atomic_sub_fetch(&g_task_count, 1, __ATOMIC_SEQ_CST);
    pthread_cond_destroy(&t->c);
    pthread_mutex_destroy(&t->m);
    free(t);
}

BaseType_t xTaskNotifyGive(TaskHandle_t h)
{
    FakeTask_t *t = h;
    pthread_mutex_lock(&t->m);
    t->notify++;
    pthread_cond_broadcast(&t->c);
    pthread_mutex_unlock(&t->m);
    return pdPASS;
}

void vTaskNotifyGiveFromISR(TaskHandle_t h, BaseType_t *woken)
{
    (void)xTaskNotifyGive(h);
    if (woken != NULL) { *woken = pdFALSE; }
}

uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t ticks)
{
    FakeTask_t *t = cur_task;
    struct timespec ts;
    uint32_t n;

    check_deleted();
    deadline(&ts, ticks);
    pthread_mutex_lock(&t->m);
    while ((t->notify == 0U) && !__atomic_load_n(&t->deleted, __ATOMIC_ACQUIRE))
    {
        int e = (ticks == portMAX_DELAY) ? pthread_cond_wait(&t->c, &t->m)
                                         : pthread_cond_timedwait(&t->c, &t->m, &ts);
        if (e == ETIMEDOUT) { break; }
    }
    n = t->notify;
    if (n > 0U) { t->notify = (clear == pdTRUE) ? 0U : (t->notify - 1U); }
    pthread_mutex_unlock(&t->m);
    check_deleted();
    return n;
}

UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t t) { (void)t; return 100U; }
int fake_task_count(void) { return g_task_count; }

/* ---- critical sections ---- */

static pthread_mutex_t crit = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
void fake_critical_enter(void) { pthread_mutex_lock(&crit); }
void fake_critical_exit(void)  { pthread_mutex_unlock(&crit); }
