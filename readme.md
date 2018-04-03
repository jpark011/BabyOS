# OS161 Implementation

## Implemented:
- Traffic light
  - 4-cross-section traffic light is simulated using 1 lock and 4 CVs.
  - Synchronization!
- System calls
  - __Process related:__ fork, exit, kill, getpid, execv
- Memory manager
  - TLB miss is handled randomly by the kernel (kick a random TLB entry)
  - There are 3 segments: coding, heap(data), and stack
  - Paging of size 4KB is used currently.
  
## TODO
- Page table
  - For now, memories are allocated only in contiguous block, which results in external fragmentation.
  - Need to changed vm, dumbvm...
- File system
  - read, writre, seek...(yeah..)
- CPU Scheduler
  - Not sure if I have control over this
