/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI - updates from inside the radio (update.h)
 *
 *  The work is x6100-update's (check, download, install, confirm, notice), run in a thread of its own so the GUI
 *  never waits for the network; what it found is said and shown from the GUI thread (scheduler_put).
 */

#include "update.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "msg.h"
#include "scheduler.h"
#include "voice.h"

#define UPDATER "/usr/sbin/x6100-update"
#define PROGRESS "/mnt/update/.progress"

typedef enum {
    UPD_IDLE = 0,
    UPD_CHECKING,
    UPD_ARMED,          /* a newer version found: the next press installs it */
    UPD_INSTALLING,
} upd_state_t;

static volatile upd_state_t state = UPD_IDLE;
static char                 latest[32];
static char                 url[512];

typedef struct {
    char text[300];
} say_t;

static void say_job(void *arg) {
    say_t *s = arg;

    voice_say_text_fmt("%s", s->text);
    msg_update_text_fmt("%s", s->text);
}

static void say(const char *fmt, ...) {
    say_t   s;
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(s.text, sizeof(s.text), fmt, ap);
    va_end(ap);
    scheduler_put(say_job, &s, sizeof(s));
}

/* x6100-update's one line of output, its exit status returned */
static int run(const char *args, char *out, size_t size) {
    char  cmd[700];
    FILE *p;
    int   st;

    snprintf(cmd, sizeof(cmd), UPDATER " %s 2>&1", args);
    p = popen(cmd, "r");
    if (out && size) out[0] = '\0';
    if (!p) return -1;
    if (out && size) {
        size_t n = fread(out, 1, size - 1, p);
        out[n] = '\0';
        while (n && (out[n - 1] == '\n' || out[n - 1] == '\r')) out[--n] = '\0';
    } else {
        char sink[256];
        while (fread(sink, 1, sizeof(sink), p) > 0) {
        }
    }
    st = pclose(p);
    return st;
}

/* A string value of a flat JSON object ("latest": "1.2.0.2"), empty when it is null or missing */
static void json_str(const char *json, const char *key, char *out, size_t size) {
    char        pat[48];
    const char *p, *e;

    out[0] = '\0';
    snprintf(pat, sizeof(pat), "\"%s\": \"", key);
    p = strstr(json, pat);
    if (!p) return;
    p += strlen(pat);
    e = strchr(p, '"');
    if (!e) return;
    snprintf(out, size, "%.*s", (int)(e - p), p);
}

static const char *error_text(const char *out) {
    return strncmp(out, "error ", 6) == 0 ? out + 6 : out;
}

static void *check_thread(void *arg) {
    static char out[4096];
    char        running[32];

    (void)arg;
    run("check", out, sizeof(out));
    if (strncmp(out, "{", 1) != 0) {
        say("Update check failed: %s", error_text(out));
        state = UPD_IDLE;
        return NULL;
    }
    json_str(out, "running", running, sizeof(running));
    json_str(out, "latest", latest, sizeof(latest));
    json_str(out, "url", url, sizeof(url));
    if (strstr(out, "\"newer\": true") && url[0]) {
        state = UPD_ARMED;
        say("Version %s is available, you have %s. Press F5 again to install it", latest, running);
    } else {
        state = UPD_IDLE;
        say("Up to date, version %s", running);
    }
    return NULL;
}

static void *install_thread(void *arg) {
    char out[600], args[600];
    int  last_pct = -1;

    (void)arg;
    say("Downloading version %s", latest);
    unlink(PROGRESS);
    /* the download in a shell of its own, its progress said every quarter */
    out[0] = '\0';
    {
        char cmd[700];
        snprintf(cmd, sizeof(cmd), "( " UPDATER " download '%s'; echo \"exit $?\" ) > /tmp/x6100-update-dl.txt 2>&1",
                 url);
        FILE *bg = popen(cmd, "r");
        if (!bg) {
            say("Update failed: the download did not start");
            state = UPD_IDLE;
            return NULL;
        }
        for (;;) {
            FILE *f = fopen(PROGRESS, "r");
            if (f) {
                char      step[16];
                long long got = 0, total = 0;
                if (fscanf(f, "%15s %lld %lld", step, &got, &total) == 3 && total > 0) {
                    int pct = (int)(got * 100 / total);
                    if (pct / 25 > last_pct / 25 && pct < 100) {
                        last_pct = pct;
                        if (pct >= 25) say("Downloading, %i percent", pct / 25 * 25);
                    }
                }
                fclose(f);
            }
            /* done when the shell has written its exit line */
            FILE *r = fopen("/tmp/x6100-update-dl.txt", "r");
            bool  done = false;
            if (r) {
                char line[600];
                while (fgets(line, sizeof(line), r)) {
                    if (strncmp(line, "exit ", 5) == 0) done = true;
                    else snprintf(out, sizeof(out), "%s", line);
                }
                fclose(r);
            }
            if (done) break;
            sleep(2);
        }
        pclose(bg);
    }
    out[strcspn(out, "\r\n")] = '\0';
    if (strncmp(out, "error", 5) == 0 || out[0] != '/') {
        say("Update failed: %s", error_text(out));
        state = UPD_IDLE;
        return NULL;
    }
    say("Installing version %s. The radio restarts its program when done", latest);
    snprintf(args, sizeof(args), "install '%s'", out);
    run(args, out, sizeof(out));
    if (strncmp(out, "ok", 2) != 0) {
        say("Update failed: %s", error_text(out));
        state = UPD_IDLE;
        return NULL;
    }
    say("Installed version %s, restarting", latest);
    return NULL;
}

void update_key(void) {
    pthread_t t;

    if (access(UPDATER, X_OK) != 0) {
        voice_say_text_fmt("Updates are not available in this build");
        return;
    }
    switch (state) {
        case UPD_IDLE:
            state = UPD_CHECKING;
            voice_say_text_fmt("Checking for an update");
            msg_update_text_fmt("Checking for an update");
            if (pthread_create(&t, NULL, check_thread, NULL) == 0) pthread_detach(t);
            else state = UPD_IDLE;
            break;
        case UPD_ARMED:
            state = UPD_INSTALLING;
            if (pthread_create(&t, NULL, install_thread, NULL) == 0) pthread_detach(t);
            else state = UPD_ARMED;
            break;
        case UPD_CHECKING:
            voice_say_text_fmt("Still checking");
            break;
        case UPD_INSTALLING:
            voice_say_text_fmt("Installing, please wait");
            break;
    }
}

static void *start_thread(void *arg) {
    char out[400];

    (void)arg;
    sleep(10);              /* after the start-up speech */
    run("notice", out, sizeof(out));
    if (strncmp(out, "ok ", 3) == 0 && out[3]) {
        say("%s", out + 3);
    }
    sleep(50);              /* a minute up: this version stays */
    run("confirm", NULL, 0);
    return NULL;
}

void update_init(void) {
    pthread_t t;

    if (access(UPDATER, X_OK) != 0) return;
    if (pthread_create(&t, NULL, start_thread, NULL) == 0) pthread_detach(t);
}
