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
 typedef struct MyVehicles
 {
   Direction origin;
   Direction destination;
 } MyVehicle;

static struct semaphore *intersectionSem;
// Lock for intersection (critical section)
static struct lock* intersectionLk;
// Tell vehicle to stop or go (Conditional Variables)
// Think like real intersection (each road has its own trafficLight)
static struct cv* trafficLights[4];
// Vehicles in the intersection
static struct array* vehicles;

// private functions
static bool right_turn(MyVehicle* v);
static bool check_constraints(MyVehicle* v);

static bool right_turn(MyVehicle* v) {
  KASSERT(v != NULL);
  return (((v->origin == west) && (v->destination == south)) ||
    ((v->origin == south) && (v->destination == east)) ||
    ((v->origin == east) && (v->destination == north)) ||
    ((v->origin == north) && (v->destination == west)));
}

static bool check_constraints(MyVehicle* v) {
  KASSERT(v != NULL);
  for (unsigned int i = 0; i < array_num(vehicles); i++) {
    MyVehicle* vi = array_get(vehicles, i)
    if (vi->origin == v->origin ||
        ( (vi->origin == v->destination) &&
          (vi->destination == v->origin)) ||
        ( (right_turn(vi) || right_turn(v)) &&
          (v->destination != vi->destination))) {
      continue;
    } else {
      return false;
    }
  }
  return true;
}

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

  vehicles = array_create();

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

  KASSERT(vehicles != NULL && array_num(vehicles) == 0);
  array_destroy(vehicles);

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
  // /* replace this default implementation with your own implementation */
  // (void)origin;  /* avoid compiler complaint about unused parameter */
  // (void)destination; /* avoid compiler complaint about unused parameter */
  // KASSERT(intersectionSem != NULL);
  // P(intersectionSem);
  KASSERT(intersectionLk != NULL);
  KASSERT(vehicles != NULL);
  KASSERT(trafficLights[origin] != NULL);

  lock_acquire(intersectionLk);

  MyVehicle* v = kmalloc(sizeof(MyVehicle));
  if (v == NULL) {
    panic("could not create vehicle");
  }
  v->origin = origin;
  v->destination = destination;

  while (!check_constraints(v)) {
    cv_wait(trafficLights[origin], intersectionLk);
  }
  // enter intersection
  array_add(vehicles, v, NULL);

  lock_release(intersectionLk);
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
  // /* replace this default implementation with your own implementation */
  // (void)origin;  /* avoid compiler complaint about unused parameter */
  // (void)destination; /* avoid compiler complaint about unused parameter */
  // KASSERT(intersectionSem != NULL);
  // V(intersectionSem);
  KASSERT(intersectionLk != NULL);
  KASSERT(vehicles != NULL);
  KASSERT(trafficLights[north] != NULL);
  KASSERT(trafficLights[east] != NULL);
  KASSERT(trafficLights[south] != NULL);
  KASSERT(trafficLights[west] != NULL);

  lock_acquire(intersectionLk);

  // find the vehicle...
  // is this the best way?? TODO
  for (unsigned int i = 0; i < array_num(vehicles); i++) {
    MyVehicle* v = array_get(vehicles, i);
    // found!
    if ((v->origin == origin) && (v->destination == destination)) {
      // Broadcase only affected sides
      if (origin == north || origin == south) {
        cv_broadcast(trafficLights[east], intersectionLk);
        cv_broadcast(trafficLights[west], intersectionLk);
      } else {
        cv_broadcast(trafficLights[north], intersectionLk);
        cv_broadcast(trafficLights[south], intersectionLk);
      }

      // bye bye
      array_remove(vehicles, i);
      kfree(v);
      break;
    }
  }

  lock_release(intersectionLk);
}
