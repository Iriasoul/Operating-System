#include <list.h>
#include <sync.h>
#include <proc.h>
#include <sched.h>
#include <assert.h>

void
wakeup_proc(struct proc_struct *proc) {
    assert(proc->state != PROC_ZOMBIE && proc->state != PROC_RUNNABLE);
    proc->state = PROC_RUNNABLE;
}

//LAB4 function3 2410668
void schedule(void)
{
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        current->need_resched = 0;
        struct proc_struct *next = idleproc;
        list_entry_t *last = (current == idleproc) ?
                            &proc_list : &(current->list_link);
        list_entry_t *le = last;
        do
        {
            le = list_next(le);
            if (le != &proc_list)
            {
                struct proc_struct *proc = le2proc(le, list_link);
                if (proc->state == PROC_RUNNABLE)
                {
                    next = proc;
                    break;
                }
            }
        } while (le != last);

        next->runs++;
        if (next != current)
        {
            proc_run(next);
        }
    }
    local_intr_restore(intr_flag);
}
