#ifndef TASK_HANDLER_H
#define TASK_HANDLER_H

//task type
#define NOTASK 0
#define DIR 1
#define PRINTALL 2
#define MATCHING 3
#define TEST 4
#define CLEARTASK 5
#define FLASHREAD 6

//task status
#define INVALID 0
#define INIT 1
#define READINGADDR 2
#define READING 3
#define PROCESSING 4
#define FINISH 5

#define TASK_LIST_SIZE 10000

unsigned int DEBUGMODE;

struct task_entry{
	unsigned int task_type;
	unsigned int start_sector; //start sector number need to handle
	unsigned int data_size; //data length in Bytes
	unsigned int sector_count; //count of sectors need to handle;if this task have subtask, then it is the sum of sector_count of all sub task to judge if it finished
	unsigned int finished_sector; //count of sectors have finished already; if it have subtask, then it is the sum of finished sub task
	unsigned int task_status;
	unsigned int cmdSlotTag; //for request return

	unsigned int parent_task_addone; //index of parent task in task list. If a task have parent task then it should not return when complete. We add 1 to index to distinguish whether it have parent task.
	unsigned int child_task_count; //indicate how many child task this task have
	unsigned int child_task_dispatched; //how many child task have been dispatched, we always dispatch child task from head so that we can use only one number to indicate how many child task have been dispatched.
	unsigned int child_task_finished; //how many child task have been finished
	unsigned int offset_in_parent; //if this task is a child task, then this variable indicate that the order of this task in parent task
};

struct task_buffer_entry{
	char buffer[16384];
};

struct task_entry* task_list;
struct task_buffer_entry* task_buffer_list;
unsigned int current_task;
unsigned int allocate_start;

char task_argument[6];

void init_tasklist();
void check_task_list();
//int insert_tasklist(unsigned int type, unsigned int start_sec, unsigned int data_size, unsigned int sector_count, unsigned int parent_task_addone, unsigned int offset_in_parent, unsigned int cmdSlotTag);
int insert_tasklist(struct task_entry* input);
int handle_task(unsigned int index, unsigned int offset_in_task, unsigned int sector_start, unsigned int sector_count, unsigned int bufAddr);

#endif
