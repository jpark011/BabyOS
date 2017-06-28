#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <kern/fcntl.h>
#include <limits.h>
#include <mips/trapframe.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <copyinout.h>
#include <kern/wait.h>
#include "opt-A2.h"


  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */
#if OPT_A2

// A02b
int sys_execv(userptr_t progname, userptr_t args) {
  struct addrspace *as, *old_as;
  struct vnode *v;
  vaddr_t entrypoint, stackptr;
  int result;
  // args parameters...
  int argc = 0;
  char** argv = kmalloc(ARG_MAX);

  // program name parameters... unknown for now
  size_t progname_length;
  char* progname_kernel = kmalloc(PATH_MAX);
  if (progname_kernel == NULL) {
    return ENOMEM;
  }

  // count number of arguments
  while (1) {
    char* arg_temp = NULL;
    copyin((const_userptr_t)args + argc * sizeof(char*), &arg_temp, sizeof(char*));
    argv[argc] = arg_temp;
    if (arg_temp == NULL) {
        break;
    }
    argc++;
  }
  // too many arguments
  if (argc > ARG_MAX / PATH_MAX) {
    return E2BIG;
  }

  // copy progname from user to kernel
  result = copyinstr((const_userptr_t)progname, progname_kernel, PATH_MAX, &progname_length);
  if (result) {
    kfree(progname_kernel);
    return result;
  }
  DEBUG(DB_SYSCALL, "copied program name: %s, length: %d\n", progname_kernel, progname_length);

  /* Open the file. */
  result = vfs_open(progname_kernel, O_RDONLY, 0, &v);
  kfree(progname_kernel);   // don't need anymore.... right...?
  if (result) {
    return result;
  }

  /* Create a new address space. */
  as = as_create();
  if (as ==NULL) {
  	vfs_close(v);
  	return ENOMEM;
  }

  /* Switch to it and activate it. */
  old_as = curproc_setas(as);
  as_activate();

  /* Load the executable. */
  result = load_elf(v, &entrypoint);
  if (result) {
    // back to old_as and free as
    curproc_setas(old_as);
    as_activate();
    as_destroy(as);
  	vfs_close(v);
  	return result;
  }

  /* Done with the file now. */
  vfs_close(v);

  // NEEED to copy args...
  /* Define the user stack in the address space */
  result = as_define_stack(as, &stackptr);
  if (result) {
    // back to old_as and free as
    curproc_setas(old_as);
    as_activate();
    as_destroy(as);
  	return result;
  }

  // destory old address sapce
  as_destroy(old_as);

  /* Warp to user mode. */
  enter_new_process(0 /*argc*/, NULL /*userspace addr of argv*/,
  		  stackptr, entrypoint);

  /* enter_new_process does not return. */
  panic("enter_new_process returned\n");
  return EINVAL;
}

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
    child_p->p_pid = curproc->p_id;
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

  lock_acquire(p_children_lock);
  // clean up ZOMBIE children (DEAD but allocated)
  for (unsigned int i = 0; i < array_num(p->p_children); i++) {
    struct proc* child = array_get(p->p_children, i);

    // is ZOMBIE?
    if (child->p_state == DEAD) {
      proc_destroy(child);
      array_remove(p->p_children, i);
      // since p->children is now mutated
      i--;
    }
  }

  // find parent first
  lock_acquire(p_table_lock);
  struct proc* parent = getProc(p_table, p->p_pid);
  lock_release(p_table_lock);
  // self destruct only when parent DNE or DEAD
  if (parent == NULL || parent->p_state == DEAD) {
    proc_destroy(p);
  }

  lock_release(p_children_lock);
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
  } else if (curproc->p_id != child->p_pid) {
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
