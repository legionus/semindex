// SPDX-License-Identifier: GPL-2.0-or-later
#define WRITE_ONCE(x, value)                             \
	do {                                             \
		*(volatile __typeof__(x) *)&(x) = value; \
	} while (0)

struct task_struct {
	int pid;
};

void set_pid(struct task_struct *task, int pid)
{
	WRITE_ONCE(task->pid, pid);
}
