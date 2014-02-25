#include <setjmp.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/signal.h>
#include "tinythreads.h"

#define NULL            0
#define DISABLE()       cli()
#define ENABLE()        sei()
#define STACKSIZE       80
#define NTHREADS        4
#define SETSTACK(buf,a) *((unsigned int *)(buf)+8) = (unsigned int)(a) + STACKSIZE - 4; \
                        *((unsigned int *)(buf)+9) = (unsigned int)(a) + STACKSIZE - 4

struct thread_block {
    void (*function)(int);   // code to run
    int arg;                 // argument to the above
    thread next;             // for use in linked lists
    jmp_buf context;         // machine state
    char stack[STACKSIZE];   // execution stack space
};

struct thread_block threads[NTHREADS];

struct thread_block initp;

thread freeQ   = threads;
thread readyQ  = NULL;
thread current = &initp;

int initialized = 0;

static void initialize(void) {
    int i;
    for (i=0; i<NTHREADS-1; i++)
        threads[i].next = &threads[i+1];
    threads[NTHREADS-1].next = NULL;

    initialized = 1;
	
	PORTB = (1 << PORTB7);		// Kolla
	EIMSK = 0x80;				// Enables Interrupts from PCINT15-8.
	PCMSK1 = 0x80;				// Enables Interrupts on pin PCINT15.
	TCCR1B = 0x0D;				// Clock prescaler set to 1024 and CFC.
	TCNT1 = 0x0;				// Clear register.
	TIMSK1 = 0x02;				// Timer output compare A interrupt
	OCR1A = 0x186;				// 8 MHz / 1024/20 -> 7812,5/20 = 390,625 cycles = 50ms

}

// Modifierad Enqueue.
static void enqueue2(thread p, thread *queue) {	// Alt #2 -> Insert first
    p->next = NULL;
	if (*queue == NULL) {
        *queue = p;
    } else {		// move *queue and set p first.
        //thread q = *queue;
        //while (q->next){
			p->next = *queue;
			*queue = p;
			//q = q->next;
		//}		
        //q->next = p;
		//*queue = p;				// Change Alt #2.
    }
}

/*

function insertBeginning(thread *queue, Thread p)
     p->next = *queue;
     *queue = p;
	 
	 
function insertBeginning(List list, Node newNode) 
     newNode.next   := list.firstNode
     list.firstNode := newNode
 
*/

// Orginal Enqueue.
static void enqueue(thread p, thread *queue) {
    p->next = NULL;
    if (*queue == NULL) {
        *queue = p;
    } else {
        thread q = *queue;
        while (q->next)
            q = q->next;
        q->next = p;
    }
}

static thread dequeue(thread *queue) {
    thread p = *queue;
    if (*queue) {
        *queue = (*queue)->next;
    } else {
        // Empty queue, kernel panic!!!
        while (1) ;  // not much else to do...
    }
    return p;
}

static void dispatch(thread next) {
    if (setjmp(current->context) == 0) {
        current = next;
        longjmp(next->context,1);
    }
}

void spawn(void (* function)(int), int arg) {
    thread newp;
    DISABLE();
    if (!initialized) initialize();
    newp = dequeue(&freeQ);
    newp->function = function;
    newp->arg = arg;
    newp->next = NULL;
    if (setjmp(newp->context) == 1) {
        ENABLE();
        current->function(current->arg);
        DISABLE();
        enqueue(current, &freeQ);
        dispatch(dequeue(&readyQ));
    }
    SETSTACK(&newp->context, &newp->stack);
    enqueue(newp, &readyQ);
    ENABLE();
}

void yield(void) {
	enqueue(current, &readyQ);
	dispatch(dequeue(&readyQ));
}

void lock(mutex *m) {
	DISABLE();
	if(m->locked == NULL){
		m->locked = 1;
	}else{
		enqueue(current, &(m->waitQ));				// Set current thread into the WaitQ
		dispatch(dequeue(&readyQ));					// Dispatch a new thread
	}
	ENABLE();
}

void unlock(mutex *m) {
	DISABLE();
	if(m->waitQ != NULL){						// Queue not empty. 
		enqueue(current,&readyQ);				// We must also save the current thread when it have passed its lock.
		dispatch(dequeue(&(m->waitQ)));			// Dispatch a thread in the WaitQ.
	}else{
		m->locked = NULL;
	}
	ENABLE();
}