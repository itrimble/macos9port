#include "os999_mobile.h"

#include <pthread.h>
#include <string.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static Os999MobileReport report = {.seq = 1, .inner = -1};
static Os999HapticFn haptic_fn;
static float pend_factor = 1.0f, pend_lines;
static int pending;

void os999_mobile_report_fold(int state, int axis, int l, int t, int r, int b) {
    pthread_mutex_lock(&lock);
    report.fold_state = state;
    report.fold_axis = axis;
    report.fold_l = l; report.fold_t = t; report.fold_r = r; report.fold_b = b;
    if (state != 0) report.inner = 1;   /* only the inner screen has a hinge */
    report.seq++;
    pthread_mutex_unlock(&lock);
}

void os999_mobile_report_display(int inner, float ppi) {
    pthread_mutex_lock(&lock);
    report.inner = inner;
    if (ppi > 0) report.ppi = ppi;
    report.seq++;
    pthread_mutex_unlock(&lock);
}

void os999_mobile_report_insets(int l, int t, int r, int b) {
    pthread_mutex_lock(&lock);
    report.inset_l = l; report.inset_t = t; report.inset_r = r; report.inset_b = b;
    report.seq++;
    pthread_mutex_unlock(&lock);
}

void os999_mobile_report_hw_keyboard(int attached) {
    pthread_mutex_lock(&lock);
    report.hw_keyboard = attached != 0;
    report.seq++;
    pthread_mutex_unlock(&lock);
}

void os999_mobile_report_tablet(int tablet) {
    pthread_mutex_lock(&lock);
    report.tablet = tablet != 0;
    report.seq++;
    pthread_mutex_unlock(&lock);
}

void os999_mobile_report_magnify(float factor) {
    if (!(factor > 0.2f && factor < 5.0f)) return;   /* drop nonsense, keep NaN out */
    pthread_mutex_lock(&lock);
    pend_factor *= factor;
    pending = 1;
    pthread_mutex_unlock(&lock);
}

void os999_mobile_report_scroll(float lines) {
    if (!(lines > -1000.0f && lines < 1000.0f)) return;
    pthread_mutex_lock(&lock);
    pend_lines += lines;
    pending = 1;
    pthread_mutex_unlock(&lock);
}

int os999_mobile_take_gestures(float *factor, float *lines) {
    pthread_mutex_lock(&lock);
    int had = pending;
    *factor = pend_factor;
    *lines = pend_lines;
    pend_factor = 1.0f;
    pend_lines = 0;
    pending = 0;
    pthread_mutex_unlock(&lock);
    return had;
}

int os999_mobile_snapshot(Os999MobileReport *out, unsigned seen_seq) {
    pthread_mutex_lock(&lock);
    *out = report;
    pthread_mutex_unlock(&lock);
    return out->seq != seen_seq;
}

void os999_mobile_set_haptics(Os999HapticFn fn) { haptic_fn = fn; }

void os999_mobile_haptic(int type, float strength, float sharpness, int duration_ms) {
    if (haptic_fn && strength > 0)
        haptic_fn(type, strength > 1 ? 1 : strength, sharpness, duration_ms);
}
