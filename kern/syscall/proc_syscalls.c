#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <mips/trapframe.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>
#include <kern/wait.h>
#include "opt-A2.h"


  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */
#if OPT_A2

int sys_fork(struct trapframe* tf, pid_t* retVal) {
    int err;
    // TODO: Should I save curproc into a var with lock???
    // create new proc
    struct proc* child_p = proc_create_runprogram(curproc->p_name);
    if (child_p == NULL) {
      panic("Can't create a new child process!");
      return ENPROC;
    }

    // copy mem
    err = as_copy(curproc_getas(), &child_p->p_addrspace);
    if (err) {
      panic("Can't copy process memory to the child!");
      proc_destroy(child_p);
      return ENOMEM;
    }

    // copy trapframe
    struct trapframe* child_tf = kmalloc(sizeof(struct trapframe));
    memcpy(child_tf, tf, sizeof(struct trapframe));

    // relate to parent
    child_p->p_parent = curproc;
    // relate to children
    spinlock_acquire(&curproc->p_lock);
    array_add(curproc->p_children, child_p, NULL);
    spinlock_release(&curproc->p_lock);

    // attach a new thread
    err = thread_fork(child_p->p_name,
                      child_p,
                      (void*)enter_forked_process, child_tf, 0);
    if (err) {
      panic("could not fork thread!");
      return err;
    }

    *retVal = child_p->p_id;
    return 0;
}
#endif // OPT_A2

void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;

  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
  (void)exitcode;

  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);
  as_deactivate();
  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */
  as = curproc_setas(NULL);
  as_destroy(as);

  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  proc_remthread(curthread);

#if OPT_A2
  // mark proc as dead
  p->p_state = DEAD;
  // save for parent wait
  p->exit_status = _MKWAIT_EXIT(exitcode);
  // wake up parent
  cv_broadcast(p->p_cv, p->p_cv_lock);

  spinlock_acquire(&p->p_lock);
  // clean up ZOMBIE children (DEAD but allocated)
  for (unsigned int i = 0; i < array_num(p->p_children); i++) {
    lock_acquire(p_table_lock);
    struct proc* child = array_get(p->p_children, i);
    lock_release(p_table_lock);

    // is ZOMBIE?
    if (child->p_state == DEAD) {
      proc_destroy(child);
      array_remove(p->p_children, i);
      // since p->children is now mutated
      i--;
    }
  }
  spinlock_release(&p->p_lock);

  // self destruct only when parent DNE or DEAD
  if (p->p_parent == NULL || p->p_parent->p_state == DEAD) {
    proc_destroy(p);
  }
#else
  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
  proc_destroy(p);
#endif
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}


/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
  /* for now, this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */
#if OPT_A2
  *retval = curproc->p_id;
#else
  *retval = 1;
#endif
  return(0);
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;

  /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.

     Fix this!
  */

  if (options != 0) {
    return(EINVAL);
  }

#if OPT_A2
  if (status == NULL) {
    panic("invalid status pointer!");
    return EFAULT;
  }

  lock_acquire(p_table_lock);
  struct proc* child = getProc(p_table, pid);
  lock_release(p_table_lock);

  if (child == NULL) {
    panic("no such process exists!");
    return ESRCH;
    // TODO: can we just compare curproc == found->parent??
  } else if (curproc != child->p_parent) {
    panic("no child exists!");
    return ECHILD;
  }

  lock_acquire(child->p_cv_lock);
  // child has NOT exited (still running...)
  while (child->p_state == ALIVE) {
    cv_wait(child->p_cv, child->p_cv_lock);
  }
  exitstatus = child->exit_status;
  lock_release(child->p_cv_lock);
#else
  /* for now, just pretend the exitstatus is 0 */
  exitstatus = 0;
#endif

  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
}
