#define _XOPEN_SOURCE 600 // Solves a OSX deprecated library problem of ucontext.h
#include <ucontext.h>

#include "../include/cthread.h"
#include "../include/cdata.h"


#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>


/*----------------------------------------------------------------------------*/
/*                                    DATA                                    */
/*----------------------------------------------------------------------------*/
int global_var_tid = 0;
ucontext_t *ending_contex = NULL;
static bool initialized = false; // Library was already initialized
TCB_t *current_running_thread = NULL;


/*----------------------------------------------------------------------------*/
/*                              GENERAL FUNCTIONS                             */
/*----------------------------------------------------------------------------*/


/// Returns a new TCB from the ready queues
/// 
TCB_t* get_new_thread() {
	TCB_t *first_thread = ready_queue_remove_and_return();
	if (first_thread == NULL) {
		CPRINT(("ERROR_CODE: first_thread == NULL\n"));
		return NULL;
	} else {
		return first_thread;
	}
}


// /// The function run_scheduler its called when the thread current in execution needs to leave execution.
// /// It handles the current_running_thread, blocking, seting the context, etc.
// /// And after this it pops another thread from the queue, and puts in execution.
int run_scheduler() {
	/* Remove current thread from the ready queue. */
	volatile bool already_swapped_context = false;

	// Leaving the Execution
	if (current_running_thread != NULL) {
		// There is a thread active
		switch(current_running_thread->state) {
			case PI_EXEC: {
				/// current_running_thread was executing but now should be
				/// re-inserted on the ready queue (Usually after a Yield).
				
				PIPRINT(("[PI]: Thread (%d) loose #10 credits\n", current_running_thread->tid));
				int cred = current_running_thread->credReal;
				cred = cred-10;
				current_running_thread->credReal = (cred > 0 ? cred : 0);

				ready_queue_insert(current_running_thread);
				getcontext(&(current_running_thread->context));
				break;
			}
			case PI_FINISHED:
				break;
			case PI_BLOCKED: {
				PIPRINT(("[PI]: Blocking thread with id: %d\n", current_running_thread->tid));
				getcontext(&(current_running_thread->context));
				break;
			}
			case PI_CREATION:
				PIPRINT(("[PI]: First time the thread ran\n"));
				break;
			case PI_READY:
				PIPRINT(("[PI]: Thread wasnt running\n"));
				break;
		}
	}
	
	// Back to Execution
	if (!already_swapped_context) {
		already_swapped_context = true;

		current_running_thread = get_new_thread(); 

		if (current_running_thread == NULL || current_running_thread->tid < 0) {
			PIPRINT(("[PI ERROR]: ERROR_CODE: f_thread == NULL || f_thread->tid < 0\n"));
			return ERROR_CODE;
		} else {
			PIPRINT(("[PI]: Thread %d is active now\n", current_running_thread->tid));
			current_running_thread->state = PI_EXEC;
    		setcontext(&(current_running_thread->context));
    		return SUCCESS_CODE;
		}
	} else {
		return SUCCESS_CODE;
	}
}



/// The function executed by the end_thread_context, on the end of each thread execution (via uc_link)
/// Besides freeing resources it unlocks threads that were previously waiting for it.
void end_thread_execution() {
	if(current_running_thread != NULL) {
		int runnig_tid = current_running_thread->tid;

		CPRINT(("[C]: Thread %d is beeing released\n", runnig_tid));
		current_running_thread->state = PROCST_TERMINO;

		free(current_running_thread);
		current_running_thread = NULL;
		// Search for a thread that is waiting for this one to finish.
		TCB_tid_waiting_t *waiting_thread = blocked_list_thread_waiting_for(runnig_tid);
		// There was a thread waiting for it to finish
		if (waiting_thread != NULL) {

			blocked_list_remove(waiting_thread);
			waiting_thread->blocked_thread->state = PROCST_APTO;

			ready_queue_insert(waiting_thread->blocked_thread);
			run_scheduler();
		} else {
			run_scheduler();
		}
	} else {
		CPRINT(("ERROR_CODE: No thread in the queue;"));
	}
}


/*----------------------------------------------------------------------------*/
/*                            LIB INITIALIZATION                              */
/*----------------------------------------------------------------------------*/

/// This function is responsable to the creation of the main thread TCB
/// and insert in the ready queue
int init_main_thread() {
	TCB_t *thread = (TCB_t*)malloc(sizeof(TCB_t));

	thread->tid = global_var_tid;
	thread->state = PROCST_CRIACAO;
	
	if (((thread->context).uc_stack.ss_sp = malloc(SIGSTKSZ)) == NULL) {
		CPRINT(("[C ERROR]: No memory for stack allocation!"));
		return ERROR_CODE;
	} else {
		(thread->context).uc_stack.ss_size = SIGSTKSZ;
		(thread->context).uc_link = NULL;
		ready_queue_insert(thread);
		getcontext(&(thread->context));
		return SUCCESS_CODE;
	}
}


// /// This initializer creates a end_thread_context, that runs the end_thread_execution function
// /// Each Thread that reaches the end, links (via uc_link) to this context, that handles desallock and unlock threads waiting
int init_end_thread_context() {
	ending_contex = (ucontext_t *) malloc(sizeof(ucontext_t));

	if (getcontext(ending_contex) != 0 || ending_contex == NULL) {
		return ERROR_CODE;
	} else {
		ending_contex->uc_stack.ss_sp = (char *) malloc(SIGSTKSZ);
		ending_contex->uc_stack.ss_size = SIGSTKSZ;
		ending_contex->uc_link = NULL;

		makecontext(ending_contex, end_thread_execution, 0);
		return SUCCESS_CODE;
	}
}


/// This function is called only one time, on the first function provided
/// by the lib.
int internal_init() {
	if (!initialized) {
		initialized = true;
		global_var_tid = 0;
		
		if (initializeAllQueues() != SUCCESS_CODE) {
			CPRINT(("ERROR_CODE: initializeAllQueues"));
			return ERROR_CODE;			
		}

		else if (init_end_thread_context() != SUCCESS_CODE) {
			CPRINT(("ERROR_CODE: init_end_thread_context"));
			return ERROR_CODE;
		}

		else if (init_main_thread() != SUCCESS_CODE) {
			CPRINT(("ERROR_CODE: init_main_thread"));
			return ERROR_CODE;
		}

		else { 
			CPRINT(("DONT UNDERSTAND: init_main_thread"));
			volatile bool main_thread_created = false;
			if (!main_thread_created) {
				CPRINT(("DONT UNDERSTAND: init_main_thread IFIF"));
				main_thread_created = true;
				run_scheduler();
			} else {
				CPRINT(("DONT UNDERSTAND: init_main_threadELSE ELSE"));
			}
			return SUCCESS_CODE;
		}
	} else {
		return ERROR_CODE;
	}
}


// /*----------------------------------------------------------------------------*/
// /*                               LIB FUNCTIONS                                */
// /*----------------------------------------------------------------------------*/


/// Criacao de um thread e insercao em uma fila de aptos
///
int ccreate (void* (*start)(void*), void *arg) {
	internal_init();
	
	int new_tid = ++global_var_tid;
	CPRINT(("[C] Creating new thread with tid: %d\n", new_tid));
	
	TCB_t *thread = (TCB_t*)malloc(sizeof(TCB_t));
	thread->tid = new_tid;
	thread->state = PROCST_CRIACAO;
	
	getcontext(&(thread->context));
	
	if (((thread->context).uc_stack.ss_sp = malloc(SIGSTKSZ)) == NULL) {
		printf("[C ERROR]: No memory for stack allocation!");
		return ERROR_CODE;
	} else {
		(thread->context).uc_stack.ss_size = SIGSTKSZ;
		(thread->context).uc_link = ending_contex;
		makecontext(&(thread->context), (void (*)(void))start, 1, arg);
		ready_queue_insert(thread);
		return new_tid;
	}
}

/// Libera a CPU voluntariamente
/// 
int cyield(void) {
	internal_init();

	if(!ready_queue_is_empty()) {
		CPRINT(("[C] Yield\n"));
		return run_scheduler();
	} else {
		return ERROR_CODE;
	}
}


/// Thread atual deve aguardar finalizacao de thread com id "tid"
/// verifica se a tid existe e apois insere a thread na lista de bloqueados
int cjoin(int tid) {
	internal_init();
	if (blocked_list_thread_waiting_for(tid) != NULL) { // Thread is already being waited by another
		return ERROR_CODE;
	} else {
		if (ready_queue_return_thread_with_id(tid) != NULL) { // Thread Exist
			TCB_tid_waiting_t *entry = (TCB_tid_waiting_t *) malloc(sizeof(TCB_tid_waiting_t));
			entry->waiting_for_thread_id = tid;
			entry->blocked_thread = current_running_thread;
		    current_running_thread->state = PROCST_BLOQ;
			blocked_list_insert(entry);
		    return run_scheduler();
		} else {
			return SUCCESS_CODE;
		}
	}
}






// /*----------------------------------------------------------------------------*/
// /*								     MUTEX       							  */
// /*----------------------------------------------------------------------------*/

/// Inicializacao do semaforo com seus valores padroes
///
int csem_init(csem_t *sem, int count) {
	// Initializing mutex
	internal_init();

	if(sem == NULL) {
		sem = (csem_t *) malloc(sizeof(csem_t));
		sem->count = count;
		return CreateFila2(sem->fila);
	} else {
		return ERROR_CODE;
	}
}


/// Tranca o semaforo se o mesmo ainda nao esta trancado, se ja estiver trancado
/// coloca a thread em uma fila de bloqueados, aguardando a liberacao do recurso
int cwait(csem_t *sem) {
	internal_init();
	CPRINT(("[C] REQUESTING SEMAPHORE WAIT"));
	if(sem != NULL){
		if (sem->count <= 0) {
			// The resouce is ALREADY being used, so we must block the thread.
			CPRINT(("Already locked\n"));

			AppendFila2(sem->fila, current_running_thread);
	    	current_running_thread->state = PROCST_TERMINO;

	    	semaphore_list_append_if_not_contained(sem);
	    	return run_scheduler();
		} else {
			// The resouce is NOT being used, so the thread is goint to use.
			CPRINT(("[C] Semaphore wasnt locked\n"));
			sem->count -= 1;
			return SUCCESS_CODE;
		}
	} else {
		return ERROR_CODE;
	}
}


/// Destrava o semaforo, e libera as threads bloqueadas esperando pelo recurso
///
int csignal(csem_t *sem) {
	internal_init();
	CPRINT(("[C] SEMAPHORE UNLOCKING: "));
	if (sem != NULL && sem->count == 0) {
		sem->count += 1;

		TCB_t *thread = (TCB_t *)semaphore_list_remove_first(sem);
		if (thread != NULL) {
			// Mutex is locked and there is threads on the blocked queue
			CPRINT(("Now Unlocking\n"));
			thread->state = PROCST_APTO;
			return ready_queue_insert(thread);
		} else {
			return SUCCESS_CODE;
		}
	} else {
		CPRINT(("[C ERROR]: Semaphore unlock error"));
		return ERROR_CODE;
	}
}