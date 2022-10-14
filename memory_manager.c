#include <linux/module.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/mm_types.h>
#include <asm/pgtable.h>
#include <linux/pid.h>
#include <linux/pid_namespace.h>
#include <linux/hrtimer.h>
#include <linux/mm.h>

//creates variable
static int pid = 0;
static int answer = 0;

//Passes parameter to variable
module_param(pid, int, 0);

// creates task list
static struct task_struct *task;
struct pid *pid_struct;
static struct vm_area_struct *vma; //list of vma area structs
static pte_t *global_pte;

//creates timer variables
static struct hrtimer timer;
unsigned long timer_interval_ns = 10e9; // 10-second timer

//Create Output Variables
int residentSetSize = 0;
int swapSize = 0;
int workingSetSize = 0;

//0 true, 1 false
int page_walk(struct task_struct *task, unsigned long address) {

	pgd_t *pgd;
	p4d_t *p4d; 
	pmd_t *pmd;
	pud_t *pud;
	pte_t *ptep;

	pgd = pgd_offset(task->mm, address);                    // get pgd from mm and the page address
	if (pgd_none(*pgd) || pgd_bad(*pgd)){           // check if pgd is bad or does not exist
		return 1;}

	p4d = p4d_offset(pgd, address);                   // get p4d from from pgd and the page address
	if (p4d_none(*p4d) || p4d_bad(*p4d)){          // check if p4d is bad or does not exist
		return 1;}

	pud = pud_offset(p4d, address);                   // get pud from from p4d and the page address
	if (pud_none(*pud) || pud_bad(*pud)){          // check if pud is bad or does not exist
		return 1;}

	pmd = pmd_offset(pud, address);               // get pmd from from pud and the page address
	if (pmd_none(*pmd) || pmd_bad(*pmd)){       // check if pmd is bad or does not exist
		return 1;} 

	ptep = pte_offset_map(pmd, address);      // get pte from pmd and the page address
	if (!ptep){return 1;}                                         // check if pte does not exist
	
	// CHECK THIS PART AND PTE POINTERS
	global_pte = ptep;
	return 0;
}

//Determine if pte was accesed and is part of the working set
int ptep_test_and_clear_young(struct vm_area_struct *vma, unsigned long addr, pte_t *ptep)
{
	int returnValue = 0;
	
	if (pte_young(*ptep)){
		//returnValue will equal 1 if the pte was accessed or 0 if not accessed
		returnValue = test_and_clear_bit(_PAGE_BIT_ACCESSED, (unsigned long *) ptep);
	}

	
	return returnValue;
}

int measure(void) {

	//Create Output Variables
	residentSetSize = 0;
	swapSize = 0;
	workingSetSize = 0;
	answer=0;
	unsigned long i;
	
	//Create task_struct
	struct task_struct *p;
	task = NULL;
	
	//to find the correct process
	for_each_process(p) {
		if (p->pid == pid) {
			task = p;
		}
	}
	
	//If task was not found exit program
	if (task == NULL) {
		printk("Task is null");
		return -1;
	}

	//Set the vma to the correct process
	vma = task->mm->mmap;

	//Loop through the given process and measure the working set size
	while(vma != NULL){		
		//Go and loop through each page with the page walk fuction
		for (i = vma->vm_start; i < vma->vm_end; i += PAGE_SIZE) {
			//Find if the page exists
			//If answer == 0 it exists, answer == 1 if it does not
			answer = page_walk(task, i);
			

			//If the page exists, see if it is part of the working set
			if (answer == 0){
				answer = ptep_test_and_clear_young(vma, i, global_pte);
				
				if (pte_present(*global_pte) == 1){//Check if pte exists in Memory
					//INCREASE RESIDENT SET SIZE
					residentSetSize++;

					//answer will equal 1 if the pte was accessed or 0 if not accessed
					workingSetSize=workingSetSize+answer;

				}
				else{
					//INCREASE SWAP VALUE BY 1
					swapSize++;
				}
			}
		}
		//Move to next page
		vma = vma->vm_next;
	}	
	return 0;
}

//Turns on the timer and measures the page walk
enum hrtimer_restart no_restart_callback(struct hrtimer *timer)
{  
	//Set up the Timer
	ktime_t currtime , interval;
	currtime  = ktime_get();
	interval  = ktime_set(0, timer_interval_ns); 
	hrtimer_forward(timer, currtime , interval);

	measure();
	
	//Print This Current PID Size Values
	printk("PID %d: RSS=%d KB, SWAP=%d KB, WSS=%d KB\n", pid, (residentSetSize*4), (swapSize*4), (workingSetSize*4));
	return HRTIMER_RESTART;
}

int memory_manager_init(void) {	
	//initializes timer
	ktime_t ktime  = ktime_set(0, timer_interval_ns); 
	hrtimer_init(&timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);  
	timer.function = &no_restart_callback;
	hrtimer_start(&timer, ktime, HRTIMER_MODE_REL);
	
	//If we got to this point, thank the lord omg
	printk("ran successfully!");
	return 0;
}

void memory_manager_exit(void) {
	hrtimer_cancel(&timer);
}

module_init(memory_manager_init);  	// defines the memory_manager_init to be called at module loading time
module_exit(memory_manager_exit);	// defines the memory_manager_exit to be called at module unload time
MODULE_LICENSE("GPL");