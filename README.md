# Garbage Collector and Coroutines for C

My experiments on runtimes and code execution.

## Garbage Collector

`gc.c, transformer.py`

An experimental mark-and-sweep garbage collector C using compiler transformations.

## Preemptive Multitasking Runtime

A pre-emptive multitasking runtime to run multiple functions on the same thread, concurrently. In other words, a
stackful coroutine implementation in C, just like Goroutines. It implements wake on IO (see `stdin_task()`), sleeps, and
a scheduler similar to Linux's default scheduler, CFS.

I started this project because I was interested in task scheduling algorithms. However, I needed a way to reliably start
and stop tasks at any point during their execution, without modifying program source code. Of course, this is easy to do
with access to bare metal. However, simulating timer interrupts and context switches in userspace in Linux is much more
roundabout.

First, I setup a timer with `getitimer()` syscall. This tells Linux to interrupt the current execution thread and call a
designated signal handler function. The signal handler will then call into the scheduler to context-switch the current
task off, and start a new task. This simulates an OS scheduler tick.

There were many bugs, however, with stack corruption, stack overflow, and timer resetting.

The first problem was that Linux prevents nested signal interrupts. It will disable all timer interrupts until the
signal handler *returns*. However, because of the scheduler implementation, the signal handler will not return until the
task is context-switched on again. Therefore, there will be only one signal handler call, then a context-switch, then
the next task will run forever. Since the signal handler never returns, Linux still thinks we're processing the signal.

I solved this by setting the `SA_NODEFER` option to `sigaction` syscall, which stops Linux from being smart and
disabling interrupts during signal handlers. This spawned the next problem, which was nested signal handlers. At high
enough alarm tick rates, the scheduler itself will get interrupted. This compounds and grows the stack until there is a
stack overflow.

To solve this, I added a global binary semaphore, such that only one signal handler can be active at any time. However,
where do I release the semaphore? If I release it just before the scheduler jumps to next task, there is still the slim
chance that the alarm could be fired after the release, but before the scheduler jumps.

Therefore, releasing the semaphore is actually done on the continued task. After the signal handler resumes, then it
will release the semaphore. This requires all context switches to happen at either the signal handler, or at functions
that will release the semaphore on continue.

```c
void sighandler(int signal) {
if (semaphore_taken()) return;
take_semaphore();
get_context1(); // This function then calls scheduler.

// Scheduler then pauses the task here, and will resume to this next line at a later point.
release_semaphore();
}
```

### Stack Protection

Since I have been burned by stack overflows many times, I wanted to do early detection of stack overflow. To do this,
I `mmap` a protected memory page below the stack. Since the stack grows downwards, if we overflow the stack, we will
overflow onto that allocated memory page. The protected memory page has `PROT_NONE`, which forces a segfault on
read/write.

To print a nice message, we actually check the stack pointer at every scheduler tick if it's within 0x800 bytes of that
protected memory page. If yes, then we print a message and early exit.

My first attempt at doing this caused a segfault at the printing stage. Since we were so close to the end of the stack,
printing the error message itself caused the stack overflow. To mitigate this, I allocated the message string in a
global, static section of the program. Then, I use `write` to FD 1 (stdout). Apparently, `write` uses less stack than
printf, which makes it ideal for this use case.

### Pausing/Continuing execution

The actual implementation of pausing a function/resuming it later on was inspired
by [this blog post](https://graphitemaster.github.io/fibers/), as well as Linux functions `getcontext`, `setcontext`,
and other coroutine implementations using `setjmp` and `longjmp`.

However, the problem with those implementations, where `get_context` returns a context struct that can be set is:

```c
void sig_handler() {
    // ... Other code 
    Context c = get_context(); // Line A
    scheduler(c); // Line B
}
```

When the scheduler goes to resume this task, it will actually jump to Line B, then execute Line B next. Therefore,
`scheduler` gets executed again, when we expected it to exit and "skip" Line B, because it has already executed. 
The primitive solution is:

```c
void sig_handler() {
    // ... Other code
    volatile bool ran_once = false;
    Context c = get_context();
    
    if(!ran_once) {
        scheduler(c);
        ran_once = true;
    } else {
        // Do nothing
    }
}
```

We maintain a variable to track if the current execution is the first execution, or the resumed execution. However, this 
method seemed error prone and complex. Therefore, I moved calling `scheduler()` right into the assembly itself. That means
the return address pushed onto the stack when I call `get_context()` is actually the next instruction when I resume. 
Now, there is no double execution of the instructions between `get_context()` and `scheduler()`.