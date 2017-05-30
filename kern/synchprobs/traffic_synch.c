#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>
#include <opt-A1.h>
#include <array.h>

/*
 * This simple default synchronization mechanism allows only vehicle at a time
 * into the intersection.   The intersectionSem is used as a a lock.
 * We use a semaphore rather than a lock so that this code will work even
 * before locks are implemented.
 */

/*
 * Replace this default synchronization mechanism with your own (better) mechanism
 * needed for your solution.   Your mechanism may use any of the available synchronzation
 * primitives, e.g., semaphores, locks, condition variables.   You are also free to
 * declare other global variables if your solution requires them.
 */

/*
 * replace this with declarations of any synchronization and other variables you need here
 */
static struct semaphore *intersectionSem;
// Lock for intersection (critical section)
static struct lock* intersectionLk;
// Tell vehicle to stop or go (Conditional Variables)
static struct cv* trafficLights[4];

/*
 * The simulation driver will call this function once before starting
 * the simulation
 *
 * You can use it to initialize synchronization and other variables.
 *
 */
void
intersection_sync_init(void)
{
  intersectionLk = lock_create("intersectionLk");
  if (intersectionLk == NULL) {
    panic("could not create intersection lock");
  }

  trafficLights[north] = cv_create("northLight");
  if (trafficLights[north] == NULL) {
    panic("could not create traffic light cv");
  }

  trafficLights[east] = cv_create("eastLight");
  if (trafficLights[east] == NULL) {
    panic("could not create traffic light cv");
  }

  trafficLights[south] = cv_create("southLight");
  if (trafficLights[south] == NULL) {
    panic("could not create traffic light cv");
  }

  trafficLights[west] = cv_create("westLight");
  if (trafficLights[west] == NULL) {
    panic("could not create traffic light cv");
  }

  /* replace this default implementation with your own implementation */

  intersectionSem = sem_create("intersectionSem",1);
  if (intersectionSem == NULL) {
    panic("could not create intersection semaphore");
  }
  return;
}

/*
 * The simulation driver will call this function once after
 * the simulation has finished
 *
 * You can use it to clean up any synchronization and other variables.
 *
 */
void
intersection_sync_cleanup(void)
{
  struct cv* light;

  KASSERT(intersectionLk != NULL);
  lock_destroy(intersectionLk);

  KASSERT(trafficLights != NULL);
  KASSERT(trafficLights[north] != NULL);
  cv_destroy(trafficLights[north]);
  KASSERT(trafficLights[east] != NULL);
  cv_destroy(trafficLights[east]);
  KASSERT(trafficLights[south] != NULL);
  cv_destroy(trafficLights[south]);
  KASSERT(trafficLights[west] != NULL);
  cv_destroy(trafficLights[west]);

  /* replace this default implementation with your own implementation */
  KASSERT(intersectionSem != NULL);
  sem_destroy(intersectionSem);
}


/*
 * The simulation driver will call this function each time a vehicle
 * tries to enter the intersection, before it enters.
 * This function should cause the calling simulation thread
 * to block until it is OK for the vehicle to enter the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle is arriving
 *    * destination: the Direction in which the vehicle is trying to go
 *
 * return value: none
 */

void
intersection_before_entry(Direction origin, Direction destination)
{
  /* replace this default implementation with your own implementation */
  (void)origin;  /* avoid compiler complaint about unused parameter */
  (void)destination; /* avoid compiler complaint about unused parameter */
  KASSERT(intersectionSem != NULL);
  P(intersectionSem);
}


/*
 * The simulation driver will call this function each time a vehicle
 * leaves the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle arrived
 *    * destination: the Direction in which the vehicle is going
 *
 * return value: none
 */

void
intersection_after_exit(Direction origin, Direction destination)
{
  /* replace this default implementation with your own implementation */
  (void)origin;  /* avoid compiler complaint about unused parameter */
  (void)destination; /* avoid compiler complaint about unused parameter */
  KASSERT(intersectionSem != NULL);
  V(intersectionSem);
}
