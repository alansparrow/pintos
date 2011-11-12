#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "devices/pit.h"
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/malloc.h"
  
/* See [8254] for hardware details of the 8254 timer chip. */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* If within the next TIMER_IDLE_TICKS ticks no wake up call is scheduled,
 * the service thread will be blocked until the next call has to be executed.
 */
#define TIMER_IDLE_TICKS 2

/* Number of timer ticks since OS booted. */
static int64_t ticks;

/* Switch that controls whether the wake up service should be running.
 * Set to false to quit the service thread. */
static bool wake_up_service = true;

/* Time of the next scheduled wake up call in ticks since boot */
static int64_t next_call = 0;

/* Thread that executes wake up calls */
static struct thread *service_thread = NULL;

/* Sorted list of wake up calls to be processed */
static struct list wake_up_calls;

/* A wake up call list element */
typedef struct wake_up_call
{
  struct list_elem elem;
  struct thread* thread;
  int64_t wake_up_ticks;
} wake_up_call;

/* Number of loops per timer tick.
   Initialized by timer_calibrate(). */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);
static void real_time_delay (int64_t num, int32_t denom);
static bool wake_up_call_less (const struct list_elem *a,
            const struct list_elem *b,
            void *aux UNUSED);

void timer_start_wake_up_service (void);
void timer_wake_up_service (void *aux UNUSED);

/* Sets up the timer to interrupt TIMER_FREQ times per second,
   and registers the corresponding interrupt. */
void
timer_init (void) 
{
  pit_configure_channel (0, 2, TIMER_FREQ);
  intr_register_ext (0x20, timer_interrupt, "8254 Timer");

  // setup list for wakeup calls
  list_init(&wake_up_calls);
}

/* Calibrates loops_per_tick, used to implement brief delays. */
void
timer_calibrate (void) 
{
  unsigned high_bit, test_bit;

  ASSERT (intr_get_level () == INTR_ON);
  printf ("Calibrating timer...  ");

  /* Approximate loops_per_tick as the largest power-of-two
     still less than one timer tick. */
  loops_per_tick = 1u << 10;
  while (!too_many_loops (loops_per_tick << 1)) 
    {
      loops_per_tick <<= 1;
      ASSERT (loops_per_tick != 0);
    }

  /* Refine the next 8 bits of loops_per_tick. */
  high_bit = loops_per_tick;
  for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
    if (!too_many_loops (high_bit | test_bit))
      loops_per_tick |= test_bit;

  printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* Returns the number of timer ticks since the OS booted. */
int64_t
timer_ticks (void) 
{
  enum intr_level old_level = intr_disable ();
  int64_t t = ticks;
  intr_set_level (old_level);
  return t;
}

/* Returns the number of timer ticks elapsed since THEN, which
   should be a value once returned by timer_ticks(). */
int64_t
timer_elapsed (int64_t then) 
{
  return timer_ticks () - then;
}

/* Sleeps for approximately TICKS timer ticks.  Interrupts must
   be turned on. */
void
timer_sleep (int64_t ticks) 
{
  ASSERT (intr_get_level () == INTR_ON);

  // Create wake up call with thread and time of wake up
  wake_up_call* w = malloc (sizeof (wake_up_call));
  w->wake_up_ticks = ticks + timer_ticks();
  w->thread = thread_current ();

  enum intr_level old_level = intr_disable();

  // Schedule wake up call in priority queue (sorted by ascending time)
  list_insert_ordered (&wake_up_calls, (struct list_elem*)w,
      wake_up_call_less, NULL);

  // Update the time of the next wake up call; this is used to wake up
  // the wake up call service thread itself, in case it is sleeping
  if (w->wake_up_ticks < next_call) next_call = w->wake_up_ticks;

  // Let this thread sleep now until it is woken up by the scheduled call
  thread_block ();

  intr_set_level(old_level);
}

/* Sleeps for approximately MS milliseconds.  Interrupts must be
   turned on. */
void
timer_msleep (int64_t ms) 
{
  real_time_sleep (ms, 1000);
}

/* Sleeps for approximately US microseconds.  Interrupts must be
   turned on. */
void
timer_usleep (int64_t us) 
{
  real_time_sleep (us, 1000 * 1000);
}

/* Sleeps for approximately NS nanoseconds.  Interrupts must be
   turned on. */
void
timer_nsleep (int64_t ns) 
{
  real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* Busy-waits for approximately MS milliseconds.  Interrupts need
   not be turned on.

   Busy waiting wastes CPU cycles, and busy waiting with
   interrupts off for the interval between timer ticks or longer
   will cause timer ticks to be lost.  Thus, use timer_msleep()
   instead if interrupts are enabled. */
void
timer_mdelay (int64_t ms) 
{
  real_time_delay (ms, 1000);
}

/* Sleeps for approximately US microseconds.  Interrupts need not
   be turned on.

   Busy waiting wastes CPU cycles, and busy waiting with
   interrupts off for the interval between timer ticks or longer
   will cause timer ticks to be lost.  Thus, use timer_usleep()
   instead if interrupts are enabled. */
void
timer_udelay (int64_t us) 
{
  real_time_delay (us, 1000 * 1000);
}

/* Sleeps execution for approximately NS nanoseconds.  Interrupts
   need not be turned on.

   Busy waiting wastes CPU cycles, and busy waiting with
   interrupts off for the interval between timer ticks or longer
   will cause timer ticks to be lost.  Thus, use timer_nsleep()
   instead if interrupts are enabled.*/
void
timer_ndelay (int64_t ns) 
{
  real_time_delay (ns, 1000 * 1000 * 1000);
}

/* Prints timer statistics. */
void
timer_print_stats (void) 
{
  printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}

/* Timer interrupt handler. */
static void
timer_interrupt (struct intr_frame *args UNUSED)
{
  ticks++;
  thread_tick ();

  if (service_thread != NULL)
    {
      // If the next scheduled wake up call is soon and the
      // service thread is sleeping, wake it up early
      if (next_call - ticks <= TIMER_IDLE_TICKS
          && service_thread->status == THREAD_BLOCKED)
        {
          enum intr_level old_level = intr_disable ();
          thread_unblock (service_thread);
          intr_set_level (old_level);
        }
    }
}

/* Returns true if LOOPS iterations waits for more than one timer
   tick, otherwise false. */
static bool
too_many_loops (unsigned loops) 
{
  /* Wait for a timer tick. */
  int64_t start = ticks;
  while (ticks == start)
    barrier ();

  /* Run LOOPS loops. */
  start = ticks;
  busy_wait (loops);

  /* If the tick count changed, we iterated too long. */
  barrier ();
  return start != ticks;
}

/* Iterates through a simple loop LOOPS times, for implementing
   brief delays.

   Marked NO_INLINE because code alignment can significantly
   affect timings, so that if this function was inlined
   differently in different places the results would be difficult
   to predict. */
static void NO_INLINE
busy_wait (int64_t loops) 
{
  while (loops-- > 0)
    barrier ();
}

/* Sleep for approximately NUM/DENOM seconds. */
static void
real_time_sleep (int64_t num, int32_t denom) 
{
  /* Convert NUM/DENOM seconds into timer ticks, rounding down.
          
        (NUM / DENOM) s          
     ---------------------- = NUM * TIMER_FREQ / DENOM ticks. 
     1 s / TIMER_FREQ ticks
  */
  int64_t ticks = num * TIMER_FREQ / denom;

  ASSERT (intr_get_level () == INTR_ON);
  if (ticks > 0)
    {
      /* We're waiting for at least one full timer tick.  Use
         timer_sleep() because it will yield the CPU to other
         processes. */                
      timer_sleep (ticks); 
    }
  else 
    {
      /* Otherwise, use a busy-wait loop for more accurate
         sub-tick timing. */
      real_time_delay (num, denom); 
    }
}

/* Busy-wait for approximately NUM/DENOM seconds. */
static void
real_time_delay (int64_t num, int32_t denom)
{
  /* Scale the numerator and denominator down by 1000 to avoid
     the possibility of overflow. */
  ASSERT (denom % 1000 == 0);
  busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000)); 
}

/* Returns true if wake up call A is earlier than B, false
   otherwise. */
static bool
wake_up_call_less (const struct list_elem *a, const struct list_elem *b,
            void *aux UNUSED)
{
  ASSERT(a != NULL);
  ASSERT(b != NULL);

  int64_t a_ticks = ((wake_up_call*)a)->wake_up_ticks;
  int64_t b_ticks = ((wake_up_call*)b)->wake_up_ticks;

  return a_ticks < b_ticks;
}

/* Starts the wake up service thread */
void timer_start_wake_up_service ()
{
  thread_create("WakeUpCallService", PRI_DEFAULT, timer_wake_up_service, NULL);
}

/* Stops the wake up service thread by setting the switch variable
 * wake_up_service to false. */
void timer_stop_wake_up_service (void)
{
  wake_up_service = false;
}

/* Checks the wake_up_calls list and wakes up sleeping threads whose
 * time has come. This function is supposed to be executed within its
 * own thread and should be started in main().
 * The thread will run until stop_wake_up_service() is called */
void timer_wake_up_service (void *aux UNUSED)
{
  service_thread = thread_current ();

  while (wake_up_service)
    {
      enum intr_level old_level = intr_disable();
      int64_t ticks = timer_ticks ();

      // Goes through the list of wake up calls in ascending order of time
      struct list_elem* e = list_begin (&wake_up_calls);
      while (e != list_end(&wake_up_calls))
        {
          wake_up_call* w = (wake_up_call*) e;

          if (w->wake_up_ticks > ticks)
            {
              // The next time a thread will want to wake up
              next_call = w->wake_up_ticks;

              // this call is in the future, so the thread will not be
              // unblocked. since all following calls will be later too,
              // the loop can be left here
              break;
            }
          else
            {
              // this call was scheduled to be executed now, so unblock
              // the thread and dispose the wake up call object
              list_pop_front(&wake_up_calls);
              thread_unblock (w->thread);
              e = list_next (e);
              free (w);
            }
        }

      // Now go to sleep until next wake up call or timer_sleep invocation
      // if the next call is not very soon
      if (next_call - ticks > TIMER_IDLE_TICKS)
        {
          thread_block ();
        }

      intr_set_level (old_level);
    }
}
