#include <sys/mman.h>

#include <stdio.h>

#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>

#pragma clang diagnostic push
#pragma ide diagnostic ignored "EndlessLoop"
struct Context {
    void *rip, *rsp, *rbx, *rbp, *r12, *r13, *r14, *r15;
};

#define NUM_TASKS 30

// The interval between each tick in milliseconds
// All sleep times must be a multiple of this tick interval.
const float TICK_MS = 10.f;


long my_clock() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000 * 1000 + t.tv_nsec / 1000;
}

struct ProtectRange {
    void *start, *end;
};


/**
 * Critical sections: disable all alarm interrupts.
 * Use when we're making a syscall, reading, or writing to a file descriptor.
 * enter_critical() itself makes a syscall, so avoid calling in a loop because it will be slow.
 */
void enter_critical();

bool is_in_critical();

void exit_critical();

/**
 * All the information a scheduler needs in one struct
 */
struct Scheduler {
    // True if the task is ready to run, false if not.
    // Used for tasks that are awaiting for IO. We use epoll() to figure out
    // which tasks can be unblocked, then set that task's ready mask on.
    bool ready_mask[NUM_TASKS];

    // File descriptor to the epoll instance we're using to track all other FD's
    int epollfd;

    // The number of sleep milliseconds that each task has left. If this number is not zero for the task,
    // then that task cannot run and must wait. At each scheduler tick, we decrement the number of seconds
    // since last tick of all tasks.
    long sleep_arr[NUM_TASKS];

    // How much "money" a task has. Derived from the Completely-Fair-Scheduler from Linux.
    // At each scheduler tick, all tasks receive (time_elapsed) / (# of tasks) money.
    // When a task runs, it uses up (time_elapsed) money.
    float moneys[NUM_TASKS];

    // Stores the continuation point, stack pointer, and return pointer for all tasks.
    struct Context tasks[NUM_TASKS];

    // The stack protection range for all tasks. If a tasks's stack pointer is close to this range,
    // exit immediately to avoid subtle bugs.
    struct ProtectRange protection[NUM_TASKS];

    // Total number of tasks
    int taskno;

    // The task that was last ran.
    int curtask;

    // Whether the scheduler tick is running. Used to prevent nested interrupts (interrupting the scheduler).
    volatile bool running;

    // Whether the scheduler has initialized yet.
    bool ready;
};

// Default, global scheduler instance
struct Scheduler sch;


// Add the file descriptor to the watchlist of a task (taskid).
// When that file descriptor has new notifications, we will set the ready_mask of that taskid,
// and the task will wake up to read.
void add_to_watchlist(int fd, int taskid) {
    struct epoll_event ev;
    ev.data.u32 = taskid;
    ev.events = EPOLLIN;
    assert(epoll_ctl(sch.epollfd, EPOLL_CTL_ADD, fd, &ev) == 0);
}

// What to print when we're overflowing the stack.
// Why not just include it in the function? When we are close to a stack overflow, we can't do anything
// as doing anything will actually trigger a stack overflow.
// Therefore, we preserve non-stack space to hold the message, then print this message from inside the near-overflowing
// function.
const char *overflow_message = "Overflow stack detected\n";

// Checks if the stack pointer is within 0x800 bytes of the end of the stack.
void stack_checker() {
    // Flag value to get the address of the stack pointer
    int stack_flag = 0;
    void *stack_ptr = &stack_flag;
    // Stack grows downwards, so we decrement.
    stack_ptr -= 0x800;
    struct ProtectRange protected = sch.protection[sch.curtask];

    // If stack pointer is within the protected section (4096 bytes), then we're screwed.
    // No way to recover, print message and exit
    if (stack_ptr <= protected.end && stack_ptr >= protected.start) {
        // Use write instead of puts/printf because it doesn't use up that much stack space...
        // Can't use stack when we have no stack.
        int _result = write(1, overflow_message, 24);

        // Force a segmentation fault here.
        volatile int a = *(int *) stack_ptr;
        exit(0);
    } else {
    }
}

/**
 * Thanks to https://graphitemaster.github.io/fibers/#setting-the-context for tutorial and reference.
 * Defined in a.asm.
 *
 * get_context: Save the stack pointer, base pointer, return address, and all non-volatile registers.
 *  Then, calls the scheduler() function.
 * set_context: Load all registers that were saved from get_context. Force write into the return address
 *      portion of the stack, so we return to a different function then that called this.
 *
 * These two functions are extremely similar to getcontext/setcontext in <ucontext.h>
 *
 * These two functions are the core that lets us execute software in a non-stack based manner. Multiple
 * tasks can run, be pre-empted, be saved, and then restarted using this scheme. It's a user-space implementation
 * of the kernel's own context switching methods.
 */
extern void get_context1();

extern void set_context(struct Context *c);


// Sets up the sleep timer, then triggers a context switch to yield to another task.
void sleep_for(float ms, const char *name) {
    long start = my_clock();
    sch.sleep_arr[sch.curtask] += ms * 1000;
    // Context switch here.
    get_context1();
    sch.running = false;

    // The task resumes here:
    long end = my_clock();
    float inaccuracy = fabsf(ms - (float) (end - start) / 1000);

    // Check if we slept for an inaccurate amount of time.
    if (inaccuracy > 0.1f) {
        printf("Inaccurate %f %s\n", inaccuracy, name);
    }
    assert(inaccuracy / (float) ms < 0.3);
}


void print(const char *c, ...) {
    va_list arg_list;
    va_start(arg_list, c);
    vprintf(c, arg_list);
    va_end(arg_list);
}

// Test task foo.
// Should print a "foo" every 3 seconds.
void foo() {
    for (int i = 0;; i++) {
        sleep_for(3000, "foo");
        print("foo %d\n", i);
    }
}


// Test task bar
// Should print a "bar" every 1 second
// Should be 3 bars for every foo.
void bar() {
    for (int i = 0;; i++) {
        sleep_for(1000, "bar");
        print("bar %d\n", i);
    }
}

// Buffer for strlen to do some CPU-intensive work
// Volatile to prevent compiler optimizations.
volatile char *do_some_work = "fda80dsh0shfdasfdsa;fa";

// test a CPU intensive task that will never willingly give up control.
// Also used to benchmark running CPU-intensive task alone, vs in this context-switching runtime.
void baz() {
    sch.running = false;
    long time = my_clock();
    unsigned long sum = 0;
    for (int i = 0;; i++) {
        sum += i % 10 + strlen((const char *) do_some_work) - 20;

        if (sum > 25000L << 19) {
            sum = 0;
            i = 0;
            printf("Baz....Time: %f\n", (my_clock() - time) / 1e6);
            time = my_clock();
        }
    }
}

// More CPU intensive tasks
// Generate a random number.
void baz1() {
    sch.running = false;
    unsigned long len = 38209;
    for (unsigned long i = 0;; i++) {
        len += strlen((const char *) do_some_work);
        len ^= (len >> 3);

        if (i % 600000000 == 0) {
            printf("Baz1...%lu rounds %lu\n", i, len);
        }
    }
}


void run_program(int index) {
    assert(index < sch.taskno);
    sch.curtask = index;
    set_context(&sch.tasks[sch.curtask]);
}

// Handle the signal from the kernel. Check the validity of the stack, then call to be context-switched out.
void sig_handler(int num) {
    if (sch.running || is_in_critical() || !sch.ready) {
        return;
    }
    sch.running = true;
    stack_checker();
    get_context1();
    sch.running = false;

    // Possible stack overflow point -- signal handler fires here while we're stuck in this stack frame.
    // Solution: use a smaller alarm tick rate
}

// Setups the interval timer for Linux kernel to interrupt our process and call the signal handler with a SIGALRM.
// This means CPU-intensive processes will still get interrupted and context-switched.
void setup_timer() {
    struct itimerval itimer;
    if (getitimer(ITIMER_REAL, &itimer)) {
        perror("Timer error");
    }
    itimer.it_interval.tv_sec = 0;
    itimer.it_interval.tv_usec = (long) (TICK_MS * 1000);
    itimer.it_value = itimer.it_interval;
    if (setitimer(ITIMER_REAL, &itimer, NULL)) {
        perror("Set timer error");
    }
    struct sigaction act = {0};
    act.sa_flags = SA_NODEFER;
    act.sa_handler = sig_handler;
    sigemptyset(&act.sa_mask);
    sigaction(SIGALRM, &act, NULL);
    sigaction(SIGSEGV, &act, NULL);
}

// Do nothing task. We must have one of these so the program doesn't exit.
void idle_task() {
    for (;;) {}
}

// A task has processed the event. Now, put it back to sleep, and call into scheduler.
void clear_ready_mask() {
    sch.ready_mask[sch.curtask] = false;
    // Easier way to force context switch with name.
    sleep_for(0, "clear-ready-mask");
}

// Uses epoll to check if any of the registered file descriptors are ready.
// If yes, then sets the ready_mask on the task.
void watch_for_io() {
    struct epoll_event events[30];
    int numfds = epoll_wait(sch.epollfd, events, 30, 0);
    if (numfds >= 0) {
        for (int i = 0; i < numfds; i++) {
            uint32_t ready_id = events[i].data.u32;
            sch.ready_mask[ready_id] = true;
            printf("Waking %d\n", ready_id);
        }
    } else {
        perror("Watch for IO failed");
    }
}

long start_time = 0;

/**
 * Scheduler implementation. Receives the recently switched-out context as an argument.
 * Calculates the time elapsed that the task has run for, decrements that tasks' money,
 * and increments all other non-running task's money.
 *
 * Decrements the sleep timers for all tasks.
 *
 * Checks for new IO event via epoll()
 *
 * Then, runs the non-sleeping, non-IO-waiting task with the maximum money.
 */
void scheduler(struct Context *c) {
    sch.running = true;

    if (start_time == 0) start_time = my_clock();
    const long now = my_clock();
    const long difference = now - start_time;
    start_time = now;

    watch_for_io();

    sch.tasks[sch.curtask] = *c;

    sch.moneys[sch.curtask] -= (float) difference;
    for (int i = 0; i < sch.taskno; i++) {
        if (sch.sleep_arr[i] > 0) {
            sch.sleep_arr[i] -= difference;
            if (sch.sleep_arr[i] <= TICK_MS / 2.0 * 1000) {
                sch.sleep_arr[i] = 0;
            }
        }
    }
    for (int i = 1; i < sch.taskno; i++) {
        sch.moneys[i] += (float) difference / sch.taskno;
    }

    float max_money = -1e37f;
    int index = -1;
    for (int i = 1; i < sch.taskno; i++) {
        if (sch.moneys[i] > max_money && sch.sleep_arr[i] == 0 && sch.ready_mask[i]) {
            max_money = sch.moneys[i];
            index = i;
        }
    }
    run_program(index);
}

int end() {
    printf("Ending\n");
    exit(0);
}


/**
 * Creates a new task from a function pointer.
 * Stack size is fixed at 8 kB and allocated using mmap.
 * At the end of the stack, we mmap a PROT_NONE page, so all reads/writes will segfault.
 *
 * I've been bitten by segmentation faults that have turned to be hidden, malignant stack overflows
 * that it's very worth to section the end of the stack as unusable.
 *
 * This also allows us to do stack protection and give an early warning if the stack nearly overflows.
 */
void new_task(void (*func)()) {
    const int STACK_SIZE = 4096 * 2;
    void *stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    void *protect_high = stack;
    void *protect_low = mmap(protect_high - 4096, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    printf("Blocked writing lower than %p \n", protect_high);
    assert(protect_low != NULL && protect_low + 4096 == protect_high);
    char *rsp = (char *) (stack + STACK_SIZE);
    rsp = (char *) ((uintptr_t) rsp & -16L);
    rsp -= 256;
    rsp -= 8;
    *((uintptr_t **) rsp) = (uintptr_t *) end;

    struct Context c;
    c.rsp = rsp;
    c.rip = (void *) func;
    if (sch.taskno + 1 < 30) {
        sch.tasks[sch.taskno] = c;
        struct ProtectRange range = {protect_low, protect_high};
        sch.protection[sch.taskno] = range;
        sch.taskno++;
    } else {
        exit(89);
    }
}

// Check for bytes from stdin, then parses the bytes into a number, and prints it out.
// Test function to make sure reading from stdin is non-blocking and works.
void poll_stdin_safe() {
    char buf[100];
    long length = read(0, &buf, 100);
    if (length <= 0) {
        perror("Read error");
        return;
    }
    int input = (int) strtol(&buf[0], NULL, 10);
    printf("Got: %d\n", input);
}

// Task that continuously reads from stdin, and prints out the number.
// Test task to make sure reading from stdin is non-blocking.
void stdin_task() {
    add_to_watchlist(0, sch.curtask);
    clear_ready_mask();
    int protect = 832083;
    while (true) {
        assert(protect == 832083);
        enter_critical();
        poll_stdin_safe();
        exit_critical();
        clear_ready_mask();

        // At this point, we are guaranteed to have data in FD 0 (stdin), because the scheduler resumed the task.
        // Then, we loop back above and read from stdin.
    }
}


int main() {
    sch.epollfd = epoll_create(1);
    // Initially, all tasks are ready.
    memset(&sch.ready_mask, 1, sizeof sch.ready_mask);
    setup_timer();

    new_task(&idle_task);
    new_task(&stdin_task);
    new_task(&foo);
    new_task(&bar);
    new_task(&baz);
    new_task(&baz1);

    sch.ready = true;
    sch.curtask = 0;

    // Jump to the first task, the idle task.
    // Then, the alarm will interrupt and call into the scheduler. After that point, we've "kickstarted"
    // the scheduler and everything is running.
    set_context(&sch.tasks[sch.curtask]);
}


// bug where things were repeating because of stack corruption, boolean variable set to true

#pragma clang diagnostic pop