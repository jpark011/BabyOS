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
#include <wait.h>
#include "opt-A2.h"


  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */
#if OPT_A2

// return proc_info OR NULL if not found
struct proc_info* findProcInfo(struct array* proc_infos, pid_t pid) {
  struct proc_info* ret = NULL;

  for (unsigned int i = 0; i < array_num(proc_infos); i++) {
    struct proc_info* now = (struct proc_info*)array_get(proc_infos, i);
    if (now->p_id == pid) {
      ret = now;
      break;
    }
  }
  return ret;
}

// return index of (proc_info) or -1 if not found
int getIndex(struct array* proc_infos, pid_t pid) {
  int ret = -1;

  for (unsigned int i = 0; i < array_num(proc_infos); i++) {
    struct proc_info* now = (struct proc_info*)array_get(proc_infos, i);
    if (now->p_id == pid) {
      ret = i;
      break;
    }
  }
  return ret;
}


int sys_fork(struct trapframe* tf, pid_t* retVal) {
    int err;
    // TODO: Should I save curproc into a var with lock???
    // create new proc
    struct proc* child_p = proc_create_runprogram( strcat(curproc->p_name, "'s child_'") );
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

    // set parent relationship
    lock_acquire(p_lock);
    struct proc_info* p_info = findProcInfo(p_table, child_p->p_id);
    p_info->pp_id = curproc->p_id;
    lock_release(p_lock);

    // attach a new thread
    err = thread_fork(strcat(child_p->p_name, "'s main_thread'"),
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

#if OPT_A2
  lock_acquire(p_lock);

  struct proc_info* p_info = findProcInfo(p_table, p->p_id);
  struct proc_info* parent = findProcInfo(p_table, p_info->pp_id);

  // parent NOT in the p_table
  if (parent == NULL) {
    // now its pid is reusable
    array_add(reusable_pids, p->p_id, NULL);
    // remove from p_table and destroy
    array_remove(p_table, getIndex(p_table, p->p_id));
    kfree(p_info);
  // there is parent interested in you!
  } else {
    p_info->state = ZOMBIE;
    p_info->exit_status = _MKWAIT_EXIT(exitcode);
    cv_broadcast(p->p_cv, p_lock);
  }
  // clean up children
  for (unsigned int i=0; i < array_num(p_table); i++) {
    struct proc_info* child = (proc_info*)array_get(p_table, i);
    // if the child is my child and a ZOMBIE we clean up
    if (child->state == ZOMBIE && child->pp_id == p->p_id) {
      array_remove(p_table, i);
      kfree(child);
    }
  }

  lock_release(p_lock);

#else
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
  (void)exitcode;
#endif

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

  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
  proc_destroy(p);

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

  lock_acquire(p_lock);
  struct proc_info* found = findProcInfo(p_table, pid);
  lock_release(p_lock);

  if (found == NULL) {
    panic("no such process exists!");
    return ESRCH;
    // TODO: can we just compare curproc == found->parent??
  } else if (curproc->p_id != found->pp_id) {
    panic("no child exists!");
    return ECHILD;
  }

  lock_acquire(p_lock);
  // child has NOT exited (still running...)
  while (found->state == ALIVE) {
    cv_wait(found->p_cv, p_lock);
  }
  exitstatus = found->exit_status;
  lock_release(p_lock);
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
