#include <stdbool.h>
#include <assert.h>
#include <bits/types/sigset_t.h>
#include <signal.h>
#include <stddef.h>

static bool g_is_critical = false;

void enter_critical() {
    assert(!g_is_critical);
    g_is_critical = true;

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGALRM);
    sigprocmask(SIG_BLOCK, &set, NULL);
}

void exit_critical() {
    assert(g_is_critical);
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGALRM);
    sigprocmask(SIG_UNBLOCK, &set, NULL);
    g_is_critical = false;
}

bool is_in_critical(){
    return g_is_critical;
}