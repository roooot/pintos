#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "devices/timer.h"
#include "threads/fixed-point.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of processes in the THREAD_READY state for MLFQS scheduling */
static struct list mlfqs_lists[PRI_MAX+1];

/* List of sleep threads. */
static struct list sleep_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* MLFQS scheduling. */
#define RECALC_FREQ 4           /* # of timer ticks to recalculation */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

/* MLFQS scheduling variables */
static fp_t load_avg;           /* System load average in mlfqs */
static int mlfqs_num_ready = 0; /* Total number of threads of in mlfqs */

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

static void mlfqs_calculate_load_avg (void);
static void mlfqs_recent_cpu_increase (void);
static void mlfqs_calculate_recent_cpu (struct thread *t);
static void mlfqs_calculate_recent_cpu_for_all (void);
static void mlfqs_calculate_priority (struct thread *t);
static void mlfqs_calculate_priority_for_all (void);

static struct thread *mlfqs_highest_priority_thread (void);

static int highest_thread_priority (void);
static int highest_thread_priority_in_locks (struct list *);

static bool cmp_thread_priority (const struct list_elem *a_, 
                                 const struct list_elem *b_, 
                                 void *aux UNUSED);
static bool cmp_thread_wakeup (const struct list_elem *a_, 
                               const struct list_elem *b_, 
                               void *aux UNUSED);

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&sleep_list);
  list_init (&all_list);

  // TODO : REMOVE HERE !!
  list_init (&ready_list);
  if (!thread_mlfqs) 
    {
      list_init (&ready_list);
    }
  else
    {
      /* MLFQS initialization */
      int i;
      for (i = 0; i < PRI_MAX + 1; i ++)
        list_init (&mlfqs_lists[i]);

      /* Initialize the system load average */
      load_avg = INT_TO_FP (0);
    }

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();
  struct list_elem *e;
  int64_t ticks = timer_ticks ();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  /* Wake up threads in sleep_list. */
  e = list_begin (&sleep_list);
  while (e != list_end (&sleep_list))
    {
      t = list_entry (e, struct thread, elem);
      if (ticks < t->wakeup) 
        break;
      e = list_remove (e);
      thread_unblock (t);
    }

  if (thread_mlfqs)
    {
      mlfqs_recent_cpu_increase ();

      /* Every second */
      if (ticks % TIMER_FREQ == 0) 
        {
          mlfqs_calculate_load_avg ();
          mlfqs_calculate_recent_cpu_for_all ();
          mlfqs_calculate_priority_for_all ();
        }
      /* Every fourth clock tick */
      else if (ticks % RECALC_FREQ == 0)
        mlfqs_calculate_priority (thread_current ());
    }

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;

  /* Add to run queue. */
  thread_unblock (t);

  /* Run immediately if higher priority */
  if (t->priority > thread_get_priority ())
    thread_yield ();

  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  if (!thread_mlfqs)
    {
      list_push_back (&ready_list, &t->elem);
    }
  else
    {
      list_push_back (&mlfqs_lists[t->priority], &t->elem);
      mlfqs_num_ready++;
    }
  t->status = THREAD_READY;
  intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  struct thread *curr = thread_current ();

  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  /* Just set our status to dying and schedule another process.
     We will be destroyed during the call to schedule_tail(). */
  intr_disable ();
  list_remove (&curr->allelem);
  curr->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *curr = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (curr != idle_thread) 
    {
      if (!thread_mlfqs)
        {
          list_push_back (&ready_list, &curr->elem);
        }
      else
        {
          list_push_back (&mlfqs_lists[curr->priority], &curr->elem);
          mlfqs_num_ready++;
        }
    }
  curr->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/* Sleep the current thread. The current thread is put to 
   sleep_list before timer interrupts */
void 
thread_sleep (int64_t until)
{
  struct thread *curr = thread_current ();
  enum intr_level old_level;

  old_level = intr_disable ();
  curr->wakeup = until;
  list_insert_ordered (&sleep_list, &curr->elem, cmp_thread_wakeup, NULL);
  thread_block ();
  intr_set_level (old_level);
}

/* Donate priority to the holder thread of lock.
   This performs nested donation. */
void
thread_priority_donate (struct thread *t)
{ 
  if (thread_mlfqs) return;

  struct thread *curr = thread_current ();
  if (curr->priority > t->highest_donated_priority)
    t->highest_donated_priority = curr->priority;

  if (t->highest_donated_priority > t->priority) 
    {
      t->priority = t->highest_donated_priority;

      if (t->acquiring_lock != NULL)
        thread_priority_donate (t->acquiring_lock->holder);
    }
}

/* Whene lock is released, the priority should be rolled back
   or should be calculate again */
void
thread_regain_priority_donation (struct thread *t)
{  
  if (thread_mlfqs) return;

  t->highest_donated_priority = 
    highest_thread_priority_in_locks (&t->locks);
  
  if (t->origin_priority > t->highest_donated_priority)
    t->priority = t->origin_priority;
  else
    t->priority = t->highest_donated_priority;
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{
  struct thread *curr = thread_current ();
 
  ASSERT (PRI_MIN <= new_priority && new_priority <= PRI_MAX);
  curr->origin_priority = new_priority;

  if (!thread_mlfqs)
    {
      /* Choose the higher priority in the donated priority 
         and the origin priority */
      if (curr->highest_donated_priority > new_priority)
        curr->priority = curr->highest_donated_priority;
      else
        curr->priority = new_priority;

      if (highest_thread_priority () > thread_get_priority ())
        thread_yield ();
    }
  else
    {
      curr->priority = new_priority;
    }
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice) 
{
  ASSERT (nice >= NICE_MIN && nice <= NICE_MAX);
  thread_current ()->nice = nice;

  mlfqs_calculate_priority (thread_current ());
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  return thread_current ()->nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  return FP_TO_INT (100 * load_avg);
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  return FP_TO_INT (100 * thread_current ()->recent_cpu);
}

/* Calculate the system load average */
static void
mlfqs_calculate_load_avg (void)
{
  int load = mlfqs_num_ready;
  
  if (thread_current () != idle_thread)
    load ++;
    
  load_avg = FP_MUL (INT_TO_FP (59) / 60, load_avg) + 
             INT_TO_FP (1) / 60 * load;
}

/* When a timer interrupt occurs, recent_cpu is increased by 1 */
static void
mlfqs_recent_cpu_increase (void)
{
  struct thread *curr = thread_current ();
  
  if (curr == idle_thread)
    return;
  
  curr->recent_cpu = INT_ADD (curr->recent_cpu, 1);
}

/* Calculate recent_cpu of target thread */
static void
mlfqs_calculate_recent_cpu (struct thread *t)
{
  ASSERT (is_thread (t));
  if (t == idle_thread)
    return;
    
  fp_t load = 2 * load_avg;
  t->recent_cpu = INT_ADD (FP_MUL (FP_DIV (load, 
    INT_ADD (load, 1)), t->recent_cpu), t->nice);
}

/* Calculate recent_cpu for all threads */
static void
mlfqs_calculate_recent_cpu_for_all (void)
{
  struct list_elem *e;
  struct thread *t;
  enum intr_level old_level; 
  
  old_level = intr_disable ();
  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      t = list_entry (e, struct thread, allelem);
      mlfqs_calculate_recent_cpu (t);
    }
  intr_set_level (old_level);
}

/* Calculate priority of target thread */
static void
mlfqs_calculate_priority (struct thread *t)
{
  ASSERT (is_thread (t));
  if (t == idle_thread)
    return;
  
  int recent = FP_TO_INT (t->recent_cpu / 4);
  int nice = t->nice * 2;
  int new_priority = PRI_MAX - recent - nice;
  
  if (new_priority > PRI_MAX)
    new_priority = PRI_MAX;
  else if (new_priority < PRI_MIN)
    new_priority = PRI_MIN;

  t->priority = new_priority;

  if (t->status == THREAD_READY)
    {
        enum intr_level old_level = intr_disable ();
        list_remove (&t->elem);
        list_push_back (&mlfqs_lists[t->priority], &t->elem);
        intr_set_level (old_level);
    }
}

/* Calculate priority for all threads */
static void
mlfqs_calculate_priority_for_all (void)
{
  struct list_elem *e;
  struct thread *t;
  enum intr_level old_level; 
  
  old_level = intr_disable ();
  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      t = list_entry (e, struct thread, allelem);
      mlfqs_calculate_priority (t);
    }
  intr_set_level (old_level);
}

/* highest priority thread in mlfqs */
static struct thread *
mlfqs_highest_priority_thread (void)
{
  int i;
  for (i = PRI_MAX; i >= PRI_MIN; i--) 
    {
      struct list *curr_list = &mlfqs_lists[i];
      if (!list_empty (curr_list)) 
        {
          return list_entry (list_front (curr_list), 
                 struct thread, elem);
        }
    }
   return NULL;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);
                    
  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Since `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;

  list_init (&t->locks);
  t->origin_priority = t->priority;

  if (thread_mlfqs)
  {
    t->nice = NICE_DEFAULT;
    if (t == initial_thread)
      t->recent_cpu = 0;
    else
      t->recent_cpu = thread_get_recent_cpu ();
  }

#ifdef USERPROG
  list_init (&t->children);
  list_init (&t->files);
#endif

  t->magic = THREAD_MAGIC;
  list_push_back (&all_list, &t->allelem);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
  if (!thread_mlfqs) 
    {
      if (!list_empty (&ready_list))
        {
          struct list_elem *e = 
                list_max (&ready_list, cmp_thread_priority, NULL);
          list_remove (e);
          return list_entry (e, struct thread, elem); 
        }
    }
  else 
    {
      struct thread *t = mlfqs_highest_priority_thread ();
      if (t != NULL)
      {
        mlfqs_num_ready--;
        list_remove (&t->elem);
        return t;
      }
    }
  return idle_thread;
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
schedule_tail (struct thread *prev) 
{
  struct thread *curr = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  curr->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && 
      prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != curr);
      palloc_free_page (prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.
   
   It's not safe to call printf() until schedule_tail() has
   completed. */
static void
schedule (void) 
{
  struct thread *curr = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (curr->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (curr != next)
    prev = switch_threads (curr, next);
  schedule_tail (prev); 
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);

/* Returns true if there is higher priority of threads in 
   ready_list than the priority of current thread, 
   false otherwise */
static int
highest_thread_priority (void)
{
  int priority = PRI_MIN;
  enum intr_level old_level = intr_disable ();
  
  if (!list_empty (&ready_list))
    {
      priority = list_entry (
        list_max (&ready_list, cmp_thread_priority, NULL), 
        struct thread, elem
      )->priority;
    }
  intr_set_level (old_level); 
  return priority;
}

/* Returns the highest priority in locks */
static int
highest_thread_priority_in_locks (struct list *list)
{
  enum intr_level old_level;
  int highest = 0;
  struct list_elem *e, *ee;
  struct semaphore *sema;
  struct thread *t;

  for (e = list_begin (list); e != list_end (list); 
       e = list_next (e))
    {
      sema = &list_entry (e, struct lock, elem)->semaphore;

      old_level = intr_disable ();
      ee = list_max (&sema->waiters, cmp_thread_priority, NULL);
      intr_set_level (old_level);

      t = list_entry (ee, struct thread, elem);

      if (t->priority > highest)
        highest = t->priority;
    }

  return highest;
}

/* Returns true if thread A is lower priority than thread B,
   false otherwise. */
static bool
cmp_thread_priority (const struct list_elem *a_, 
                     const struct list_elem *b_, void *aux UNUSED)
{
  const struct thread *a = list_entry (a_, struct thread, elem);
  const struct thread *b = list_entry (b_, struct thread, elem);

  return a->priority < b->priority;
}

/* Returns true if thread A is wake up early than thread B,
   false otherwise. */
static bool
cmp_thread_wakeup (const struct list_elem *a_, 
                   const struct list_elem *b_, void *aux UNUSED)
{
  const struct thread *a = list_entry (a_, struct thread, elem);
  const struct thread *b = list_entry (b_, struct thread, elem);

  return a->wakeup < b->wakeup;
}
