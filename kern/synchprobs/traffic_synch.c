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
static struct array* trafficLights;

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
  // temp
  struct cv* light;

  intersectionLk = lock_create("intersectionLk");
  if (intersectionLk == NULL) {
    panic("could not create intersection lock");
  }

  trafficLights = array_create();

  light = cv_create("northLight");
  if (light == NULL) {
    panic("could not create traffic light cv");
  }
  array_add(trafficLights, light, NULL);

  light = cv_create("southLight");
  if (light == NULL) {
    panic("could not create traffic light cv");
  }
  array_add(trafficLights, light, NULL);

  light = cv_create("eastLight");
  if (light == NULL) {
    panic("could not create traffic light cv");
  }
  array_add(trafficLights, light, NULL);

  light = cv_create("westLight");
  if (light == NULL) {
    panic("could not create traffic light cv");
  }
  array_add(trafficLights, light, NULL);

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
  int i;
  struct cv* light;

  KASSERT(intersectionLk != NULL);
  lock_destroy(intersectionLk);
  KASSERT(trafficLights != NULL);
  for (i=0; (unsigned)i < array_num(trafficLights); i++) {
    light = array_get(trafficLights, (unsigned)i);
    KASSERT(light != NULL);
    cv_destroy(light);
  }
  array_destroy(trafficLights);

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
