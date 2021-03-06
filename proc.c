#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
int sched_policy = 1;                         // 1  MLFQ Scheduler
struct proc* q0[64];
struct proc* q1[64];
int c0=0,c1=0;

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;

int sched_trace_enabled =1 ; // for CS550 CPU/process project

extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
  cpu -> var = 2;
  cpu -> var1 = 4;
  //cprintf("threshold %d\n",cpu->var);
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;
  p->priority = 4;
  p->runticks = 0;
  p->waitticks= 0;
 
  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  p->priority = 4;
  p->runticks = 0;
  p->waitticks= 0;
  p->flag = 0;
  q0[c0] = p;
  c0++;
  
  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;
  
  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;
  
  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];
  
  p = allocproc();
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  
  sz = proc->sz;
  if(n > 0){
    if((sz = allocuvm(proc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(proc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  proc->sz = sz;
  switchuvm(proc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;

  // Allocate process.
  if((np = allocproc()) == 0)
    return -1;

  // Copy process state from p.
  if((np->pgdir = copyuvm(proc->pgdir, proc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = proc->sz;
  np->parent = proc;
  *np->tf = *proc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(proc->ofile[i])
      np->ofile[i] = filedup(proc->ofile[i]);
  np->cwd = idup(proc->cwd);

  safestrcpy(np->name, proc->name, sizeof(proc->name));
 
  pid = np->pid;

  // lock to force the compiler to emit the np->state write last.
  acquire(&ptable.lock);
  np->state = RUNNABLE;
  release(&ptable.lock);
  
  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *p;
  int fd;

  if(proc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(proc->ofile[fd]){
      fileclose(proc->ofile[fd]);
      proc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(proc->cwd);
  end_op();
  proc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(proc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == proc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  proc->state = ZOMBIE;
  for(int i=0;i < c0 ; i++)
  {
	  if(proc->pid == q0[i]->pid)
	  {	
	  	int index;
	  	index = i;
	  	for(int j=index;j< c0-1;j++)
	  	{
	  		q0[j] = q0[j+1];
	  	}
	  	c0--;
	  }
  }
  for(int l=0;l < c1 ; l++)
  {
	  	if(proc->pid == q1[l]->pid)
	  	{
		  	int index;
		  	index = l;
		  	for(int k=index;k< c1-1;k++)
		  	{
		  		q1[k] = q1[k+1];
		  	}
		  	c1--;	
		}	
  }	  
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;

  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for zombie children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != proc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->state = UNUSED;
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || proc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(proc, &ptable.lock);  //DOC: wait-sleep
  }
}


//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.




void
scheduler(void)
{
  //struct proc *pnext;
  struct proc *p;
  
  int ran = 0,i,j; // CS550: to solve the 100%-CPU-utilization-when-idling problem

  for(;;)
  {
    // Enable interrupts on this processor.
    sti();
	
	//struct proc *highP = NULL;
    // Loop over process table looking for process to run.
    
    if(sched_policy == 0)
    {
    	acquire(&ptable.lock);
    	ran = 0;
		for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
		{
		  if(p->state != RUNNABLE)
		    continue;

		  ran = 1;
		  
		  // Switch to chosen process.  It is the process's job
		  // to release ptable.lock and then reacquire it
		  // before jumping back to us.
		  proc = p;
		  switchuvm(p);
		  p->state = RUNNING;
		  swtch(&cpu->scheduler, proc->context);
		  switchkvm();
		  
		  if(p ->pid > 2)
	  	    {
	  	    	//p->runticks++;
	  	    	//cprintf("running ticks for process %d \n",p->state);
	  	    }

		  // Process is done running for now.
		  // It should have changed its p->state before coming back.
		  proc = 0;
		}
    	release(&ptable.lock);
    	
    	if (ran == 0)
		{
		    halt();
		}
    }	
	

     //--*priority based scheduler*--//
	else 
	{
		//cprintf("Testing\n");
		acquire(&ptable.lock);
    	ran = 0;
		
	  	for(i=0; i < c0; i++)
	  	{
	  		p = q0[i];
	  		if(p -> state != RUNNABLE)
	  		{
	  			continue;
	  		}
	  		
	  		 ran = 1;
	  		
 		
		  	proc = p;
	 	    switchuvm(p);
			p->state = RUNNING;
			swtch(&cpu->scheduler, proc->context);
			switchkvm();
			//cprintf("process executing in c0 is %d \n",p->pid);
			
			
			if(p ->pid > 2)
	  	    {
	  	    	if(p->flag == 0)
	  	    	{
	  	    		p->runticks++;
		  	    	//cprintf("running ticks for process %d \n",p->state);
		  	    }	
		  	  // p->flag =1
		  	    
	  	    }
	  	    
	  	    for(int d=0;d<c1;d++)
		  	{
		  	    q1[d] -> waitticks++;
		  	    
		  	    if(q1[d] -> waitticks > cpu->var1)
			  	{
			  	    	
			  	    q0[c0] = q1[d];
			  	    q0[c0] -> waitticks = 0;
			  	    c0++;  
			  	    for(j=i;j< c1 - 1;j++)
			  	    {
			  	    	q1[j] = q1[j+1];
			  	    }
			  	    c1--;	                                                        
			  	}
		  	}

	  	    
	  	    if(p->runticks > cpu->var)
	  	    {
	  	    	
	  	    	q1[c1] = p;
	  	    	q1[c1] -> runticks = 0;
	  	    	c1++;  
	  	    	
	  	    	for(j=i;j< c0 - 1;j++)
	  	    	{
	  	    		q0[j] = q0[j+1];
	  	    		//cprintf("TESTING2 %d \n",q0[j]);
	  	    	}
	  	    	c0--;
	  	    		                                                        
	  	    }
	  	    
	  	  
			  // Process is done running for now.
			  // It should have changed its p->state before coming back.
			 proc = 0;
	 	 }	
    	int bit = 0;
	 	for(int x=0;x < c0; x++)
	 	{
	 		
	 		p = q0[x];
	 		if(p->state != RUNNABLE)
	 		{
	 			 	bit = 1;
	 		}
	 		else
	 		{
	 			bit = 0;
	 			break;
	 		}
	 		
    	}
	 	if((bit == 1) && (c1 > 0))
	 	{
	 			//cprintf("Testing6-----\n");
	 			int d,max,max_index=0;
	 			max = q1[0]->waitticks;
	 			
	 			
	 			for(d=0;d<c1;d++)
	 			{
	 				if(max < q1[d]->waitticks)
	 				{
	 					max = q1[d]->waitticks;
	 					max_index = d;
	 					
	 				}	
	 			}
	 			
	 			
		 		p = q1[max_index];
		 		
		  		/*if(p -> state != RUNNABLE)
		  		{
		  			continue;
		  		}*/
		  		//cprintf("Testing 10-----\n");
		  		ran = 1;
		  		
		  	    proc = p;
		  	    

		  	    
		  	    //cprintf("wait ticks for process are %d",p->waitticks);
		  	    
		  	    switchuvm(p);
		  	    p->state = RUNNING;
				swtch(&cpu->scheduler, proc->context);
				switchkvm();

				for(d=0;d<c1;d++)
		  	    {
		  	    	if (p -> pid != q1[d] -> pid)
		  	    	{
		  	    		q1[d]->waitticks++;
		  	    	}
		  	    }

				//cprintf("process executing in c1 is %d \n", p->pid);
		  	    if(p->waitticks > cpu->var1)
		  	    {
		  	    	
		  	    	q0[c0] = p;
		  	    	q0[c0] -> waitticks = 0;
		  	    	c0++;  
		  	    	for(j=i;j< c1 - 1;j++)
		  	    	{
		  	    		q1[j] = q1[j+1];
		  	    	}
		  	    	c1--;	                                                        
		  	    }
		  	  
			

				  // Process is done running for now.
				  // It should have changed its p->state before coming back.
				proc = 0;
		     	 	
	 	}
  		release(&ptable.lock);
  		if (ran == 0)
		{
		    halt();
		}
  	 }	
  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state.
void
sched(void)
{
  int intena;

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(cpu->ncli != 1)
    panic("sched locks");
  if(proc->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = cpu->intena;

  // CS550: print previously running process
  // We skip process 1 (init) and 2 (shell)
  if ( sched_trace_enabled &&
	proc && proc->pid != 1 && proc->pid != 2)
        cprintf("[%d]", proc->pid);

  swtch(&proc->context, cpu->scheduler);
  cpu->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  proc->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot 
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }
  
  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  if(proc == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }

  // Go to sleep.
  proc->chan = chan;
  proc->state = SLEEPING;
  sched();

  // Tidy up.
  proc->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];
  
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

int systempriority(int pid,int priority)
{
	struct proc *p;
	for(int i= 0; i< c0 ; i++)
	{
		p = q0[i];
		if(p->pid == pid)
		{
			p-> priority = priority;	
			if(priority == 0)
			{
				p->flag = 1;
				p->runticks = 0;
			}
			else 
			{
				p->flag = 0;	
			}
		}
		
	}
	for(int j=0;j < c1;j++)
	{
		p = q1[j];
		if(p->pid == pid)
		{
			p-> priority = priority;	
			if(priority == 0)
			{
					q0[c0] = p;
		  	    	c0++;  
		  	    	for(int l=j;l< c1 - 1;l++)
		  	    	{
		  	    		q1[l] = q1[l+1];
		  	    	}
		  	    	c1--;
					p->flag = 1;
			}
			else 
			{
				p->flag = 0;	
			}
		}
		
	}
	return 0;	
}
	


/*int systempriority(int pid,int priority)
{
	struct proc *p;
	acquire(&ptable.lock);
	
	for(p = ptable.proc; p< &ptable.proc[NPROC]; p++)
	{
		if(p->pid == pid)
		{
			p-> priority = priority;
			proc->state = RUNNABLE;
			sched();
			release(&ptable.lock);
			return 0;
		}
	}
	for(p = ptable.proc; p< &ptable.proc[NPROC]; p++)
	{
		release(&ptable.lock);
		return -1;
	}
	return 0;
}*/
	
	
	
	
	

