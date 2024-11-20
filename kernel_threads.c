
#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"
#include "kernel_cc.h"
#include "kernel_streams.h"


/** 
  @brief Create a new thread in the current process.
  */
Tid_t sys_CreateThread(Task task, int argl, void* args)
{
  PCB *currproc = CURPROC;
  
  /*initiate the current PTCB*/
  PTCB *currptcb = ptcb_Init(task, argl, args);

  /*insert the current PTCB in the PTCB_list of the current PCB */
  rlist_push_front(& currproc->ptcb_list,& currptcb->ptcb_list_node);

  TCB *currtcb = spawn_thread(currproc, start_thread, currptcb);

  currproc->thread_count ++;

  wakeup(currtcb);

  return (Tid_t)currptcb;
}

/**
  @brief Return the Tid of the current thread.
 */
Tid_t sys_ThreadSelf()
{
	return (Tid_t) cur_thread()->ptcb;
}

/**
  @brief Join the given thread.
  */
int sys_ThreadJoin(Tid_t tid, int* exitval)
{
  PTCB* currptcb = (PTCB*)tid;
  PCB* runningpcb = CURPROC;
  rlnode* node = rlist_find(&runningpcb->ptcb_list, currptcb, NULL);

  /*The given thread has an illegal Tid*/
  if(tid == NOTHREAD)
    return -1;
  /*A thread can't join itself*/
  else if(tid == sys_ThreadSelf())
    return -1;
  /*The current PTCB doesn't exist in the PCB's PTCB_list*/
  else if(node == NULL)
    return -1;
  
  /* The caller of the function joins the thread in case it's not */
    
  node->ptcb->refcount++;

  while(node->ptcb->exited == 0 && node->ptcb->detached == 0){ 
    /* Wait until the joined thread calls broadcast */   
    kernel_wait(&node->ptcb->exit_cv, SCHED_USER); 
  }

  node->ptcb->refcount--;
  
  /* The joined thread became detached */
  if(node->ptcb->detached == 1)
    return -1;

  /* The joined thread became exited */
  if(exitval!=NULL) 
    (*exitval) = node->ptcb->exitval;
  
  /*The current PTCB is removed by the last thread*/
  if(node->ptcb->refcount == 0){
      rlist_remove(&node->ptcb->ptcb_list_node);
      free(node->ptcb);
  }  

  return 0;
  
}

/**
  @brief Detach the given thread.
  */
int sys_ThreadDetach(Tid_t tid)
{
  PTCB* currptcb = (PTCB*)tid;
  PCB* runningpcb = CURPROC;
  rlnode* node = rlist_find(&runningpcb->ptcb_list, currptcb, NULL);
  
  /*The given thread has an illegal Tid*/
  if(tid == NOTHREAD)
    return -1;
  /*The current PTCB doesn't exist in the PCB's PTCB_list*/
  else if(node == NULL)
    return -1;
  /* In this case the given thread is exited*/
  else if(currptcb->exited == 1){
    return -1;
  }
  else{
    currptcb->detached = 1;
    kernel_broadcast(&currptcb->exit_cv);
    return 0;
  }
  return -1;
}

/**
  @brief Terminate the current thread.
  */
void sys_ThreadExit(int exitval)
{
  PCB* curproc = CURPROC; /* cache for efficiency */
  TCB* curthread = cur_thread();
  

 /*one last thread remains in the current PCB*/
  if (curproc->thread_count == 1)
  {  
    rlnode *currlist = &curthread->owner_pcb->ptcb_list;

    /* delete the ptcbs*/
    while (rlist_len(currlist) > 0)
    {
      rlist_remove(currlist->next);
      free(currlist->next->ptcb);
    }
 
    /* Reparent any children of the exiting process to the 
       initial task */

    PCB* initpcb = get_pcb(1);
    while(!is_rlist_empty(& curproc->children_list)) {
      rlnode* child = rlist_pop_front(& curproc->children_list);
      child->pcb->parent = initpcb;
      rlist_push_front(& initpcb->children_list, child);
    }

    /* Add exited children to the initial task's exited list 
       and signal the initial task */
    if(!is_rlist_empty(& curproc->exited_list)) {
      rlist_append(& initpcb->exited_list, &curproc->exited_list);
      kernel_broadcast(& initpcb->child_exit);
    }
   
    /* Put me into my parent's exited list (init process shound not be included
       because it has no parent) */
  
    if(get_pid(curproc)!=1){  
    rlist_push_front(& curproc->parent->exited_list, &curproc->exited_node);
    kernel_broadcast(& curproc->parent->child_exit);
    }
  

  assert(is_rlist_empty(& curproc->children_list));
  assert(is_rlist_empty(& curproc->exited_list));


  /* 
    Do all the other cleanup we want here, close files etc. 
   */

  /* Release the args data */
  if(curproc->args) {
    free(curproc->args);
    curproc->args = NULL;
  }

  /* Clean up FIDT */
  for(int i=0;i<MAX_FILEID;i++) {
    if(curproc->FIDT[i] != NULL) {
      FCB_decref(curproc->FIDT[i]);
      curproc->FIDT[i] = NULL;
    }
  }

  curthread->ptcb->exitval = exitval;
  curthread->ptcb->exited = 1;
  curproc->thread_count--;

  /* Disconnect my main_thread */
  curproc->main_thread = NULL;

  /* Now, mark the process as exited. */
  curproc->pstate = ZOMBIE;

  /* Bye-bye cruel world */
  kernel_sleep(EXITED, SCHED_USER);
 

 }

/* More than one threads remain in the current PCB */
else{
  curthread->ptcb->exitval = exitval;
  curthread->ptcb->exited = 1;
  curproc->thread_count--;

  /* The current PTCB is detached, so it should be deleted */
  if (curthread->ptcb->detached == 1)
  {
    rlist_remove(&curthread->ptcb->ptcb_list_node);
    free(curthread->ptcb);
  }
  /* The current PTCB is not detached, so it should wakeup every thread who joined it
    because it's exiting */
  else
    kernel_broadcast(&curthread->ptcb->exit_cv);

  /* Bye-bye cruel world */
  kernel_sleep(EXITED, SCHED_USER);
}

}
