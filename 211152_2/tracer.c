#include "include/context.h"
#include "include/file.h"
#include "include/schedule.h"
#include <context.h>
#include <memory.h>
#include <lib.h>
#include <entry.h>
#include <file.h>
#include <tracer.h>

int debug = 0;
int debug2 = 0;
///////////////////////////////////////////////////////////////////////////
//// 		Start of Trace buffer functionality 		      /////
///////////////////////////////////////////////////////////////////////////

int is_valid_mem_range(unsigned long buff, u32 count, int access_bit) 
{
	u32 mask = 1 << access_bit;

	struct exec_context *ctx = get_current_ctx();
	if(ctx == NULL) {  // Check if current context is valid
		return -EINVAL;
	}

	// Check memory segments
	struct mm_segment *mm_seg = ctx->mms;
	for(int region = MM_SEG_CODE; region < MAX_MM_SEGS; region++) {
		if(debug) printk("[BUFFER CHECK] Checking memseg area %d %d %x %x %x %x\n", mask, mm_seg[region].access_flags, buff, buff+count, mm_seg[region].start, mm_seg[region].end);

		if(mm_seg[region].access_flags & mask) {  // Check if memory segment is valid
			if(region != MM_SEG_STACK && buff >= mm_seg[region].start && buff + count <= mm_seg[region].next_free) {  // Check if buffer is in memory segment
				return 0;
			}
			if(region == MM_SEG_STACK && buff >= mm_seg[region].start && buff + count <= mm_seg[region].end) {  // Check if buffer is in stack segment
				return 0;
			}
		}
	}

	if(debug) printk("[BUFFER CHECK] Memseg area checked\n");
	// Check vm area segments
	int at_start = 1;
	struct vm_area *vm_ar = ctx->vm_area;
	struct vm_area *vm_start = ctx->vm_area;
	while(vm_ar != NULL && (vm_ar != vm_start || at_start == 1)) {
		at_start = 0;
		if(debug) printk("[BUFFER CHECK] Checking vm area %d %d %x %x %x %x\n", mask, vm_ar->access_flags, buff, buff+count, vm_ar->vm_start, vm_ar->vm_end);

		if(vm_ar->access_flags & mask) {  // Check if vm area is valid
			if(buff >= vm_ar->vm_start && buff + count <= vm_ar->vm_end) {  // Check if buffer is in vm area
				return 0;
			}
		}
		vm_ar = vm_ar->vm_next;
	}

	return -EBADMEM;
}

long trace_buffer_close(struct file *filep)
{
	// if(debug3) printk("[FREEING FPTR] %x\n", filep);
	if(filep == NULL || filep->type != TRACE_BUFFER) {  // Check if file pointer is valid
		return -EINVAL;
	}
	if(filep->trace_buffer == NULL) {
		return -EINVAL;
	}
	if(filep->trace_buffer->buffer == NULL) {
		return -EINVAL;
	}
	os_page_free(USER_REG, filep->trace_buffer->buffer);  // Free trace buffer
	
	os_free(filep->trace_buffer, sizeof(struct trace_buffer_info));  // Free trace buffer info;
	if(filep->fops == NULL) {
		return -EINVAL;
	}
	os_free(filep->fops, sizeof(struct fileops)); // Free file operations
	os_free(filep, sizeof(struct file));  // Free file

	// int f=0;
	// struct exec_context *current = get_current_ctx();
	// for(int i=0;i<MAX_OPEN_FILES;i++){
	// 	printk("[FILES %d] %x\n" , i, current->files[i]);
	// 	// if(current->files[i]==filep){
	// 	// 	current->files[i]=0;
	// 	// 	f=1;
	// 	// }
	// }
	// if(f==0)return -EINVAL;
	return 0;	
}

int trace_buffer_read(struct file *filep, char *buff, u32 count)
{
	// int f=0;
	// struct exec_context *current = get_current_ctx();
	// for(int i=0;i<MAX_OPEN_FILES;i++){
	// 	printk("[FILES %d] %x\n" , i, current->files[i]);
	// 	// if(current->files[i]==filep){
	// 	// 	current->files[i]=0;
	// 	// 	f=1;
	// 	// }
	// }

	// if(f==0)return -EINVAL;
	if(filep == NULL || filep->type != TRACE_BUFFER) {  // Check if file pointer is valid
		return -EINVAL;
	}
	if(filep->mode == O_WRITE) {  // Check if file is opened in write mode
		return -EINVAL;
	}
	if(count < 0 ) {
		return -EINVAL;
	}
	// if(buff == NULL) {  // Check if buffer is valid
	// 	return -EINVAL;
	// }

	struct trace_buffer_info *trace_buf = filep->trace_buffer;
	if(trace_buf == NULL) {  // Check if trace buffer is valid
		return -EINVAL;
	}

	if(debug) printk("[TRACER READ MEM] Is valid mem range? %d\n", is_valid_mem_range((unsigned long) buff, count, 0));
	if(is_valid_mem_range((unsigned long) buff, count, 1) != 0) {  // Check if buffer is valid
		return -EBADMEM;
	}

	int rcount = 0;
	while(rcount < count) {
		if(debug) printk("[TRACER READ] Running read loop with rcount %d\n", rcount);
		if(trace_buf->read_ptr == trace_buf->write_ptr && !trace_buf->is_full) {  // Check if trace buffer is empty
			break;
		}
		*(buff + rcount) = *(trace_buf->buffer + trace_buf->read_ptr);  // Read from trace buffer
		trace_buf->is_full = 0;
		trace_buf->read_ptr = (trace_buf->read_ptr + 1) % TRACE_BUFFER_MAX_SIZE;  // Increment read pointer
		rcount++;
	}
	return rcount;
}

int trace_buffer_write(struct file *filep, char *buff, u32 count)
{
	if(filep == NULL || filep->type != TRACE_BUFFER) {  // Check if file pointer is valid
		return -EINVAL;
	}
	if(filep->mode == O_READ) {  // Check if file is opened in read mode
		return -EINVAL;
	}
	if(count < 0) {
		return -EINVAL;
	}
	// if(buff == NULL) {  // Check if buffer is valid
	// 	return -EINVAL;
	// }

	struct trace_buffer_info *trace_buf = filep->trace_buffer;
	if(trace_buf == NULL) {  // Check if trace buffer is valid
		return -EINVAL;
	}

	if(is_valid_mem_range((unsigned long) buff, count, 0) != 0) {  // Check if buffer is valid
		return -EBADMEM;
	}

	int wcount = 0;
	while(wcount < count) {
		if(debug) printk("[TRACER WRITE] Running write loop with wcount %d\n", wcount);
		// if(trace_buf->write_ptr == trace_buf->read_ptr) {  // Check if trace buffer is full
		// 	break;
		// }
		if(trace_buf->read_ptr == trace_buf->write_ptr && trace_buf->is_full) {  // Check if trace buffer is empty
			break;
		}
		*(trace_buf->buffer + trace_buf->write_ptr) = *(buff + wcount);  // Write to trace buffer
		wcount++;
		if(((trace_buf->write_ptr + 1) % TRACE_BUFFER_MAX_SIZE) == trace_buf->read_ptr) {
			trace_buf->is_full = 1;
			trace_buf->write_ptr = (trace_buf->write_ptr + 1) % TRACE_BUFFER_MAX_SIZE;
			break;
		}
		trace_buf->write_ptr = (trace_buf->write_ptr + 1) % TRACE_BUFFER_MAX_SIZE;  // Increment write pointer
	}
  return wcount;
}

int sys_create_trace_buffer(struct exec_context *current, int mode)
{
	if(current == NULL) {
		return -EINVAL;
	}
	if(current->files == NULL) {
		return -EINVAL;
	}
	// char *bufff = (char *)os_page_alloc(USER_REG);
	// bufff[0] = 'a';
	// bufff[1] = 'b';
	// bufff[2] = '\0';
	// if(1) printk("[TRACER] Is valid mem range? %s\n", bufff);
	// os_page_free(USER_REG, bufff);
	// if(1) printk("[TRACER] Is valid mem range? %s\n", bufff);
	int fd = -1;
	for(int i = 0; i < MAX_OPEN_FILES; i++) {
		if(current->files[i] == NULL) {
			fd = i;
			break;
		}
	}
	if(fd == -1) {  // Check if fd found or not
		return -EINVAL;
	}

	struct file *fptr = (struct file *)os_alloc(sizeof(struct file));
	if(fptr == NULL || fptr == (void *)-1) {  // Check if page allocation failed
		return -ENOMEM;
	}
	fptr->type = TRACE_BUFFER;

	if(mode != O_READ && mode != O_WRITE && mode != O_RDWR) {  // Check valid mode
		return -EINVAL;
	}
	fptr->mode = mode;  // Set mode
	fptr->offp = 0;  // Set offset to 0
	fptr->ref_count = 1;  // Set reference count to 1
	fptr->inode = NULL;  // Set inode to NULL

	struct trace_buffer_info *trace_buffer = (struct trace_buffer_info *)os_alloc(sizeof(struct trace_buffer_info));
	if(trace_buffer == NULL || trace_buffer == (void *)-1) {  // Check if page allocation failed
		return -ENOMEM;
	}

	trace_buffer->buffer = (char *)os_page_alloc(USER_REG);  // Allocate trace buffer
	trace_buffer->read_ptr = 0;  // Set read pointer to 0
	trace_buffer->write_ptr = 0;  // Set write pointer to 0
	trace_buffer->is_full = 0;  // Trace buffer is empty

	fptr->trace_buffer = trace_buffer;  // Set trace buffer

	struct fileops *fops = (struct fileops *)os_alloc(sizeof(struct fileops));
	if(fops == NULL || fops == (void *)-1) {  // Check if page allocation failed
		return -ENOMEM;
	}
	fops->read = trace_buffer_read;  // Set read function
	fops->write = trace_buffer_write;  // Set write function
	fops->lseek = 0;  // No lseek for trace buffer
	fops->close = trace_buffer_close;  // Set close function
	fptr->fops = fops;  // Set file operations

	current->files[fd] = fptr;  // Set file pointer in current context
	// if(debug3) printk("[ALLOCED FPTR] %x\n", fptr);
	return fd;
}

///////////////////////////////////////////////////////////////////////////
//// 		Start of strace functionality 		      	      /////
///////////////////////////////////////////////////////////////////////////

int trace_buffer_write_os(struct file *filep, char *buff, u32 count)
{
	if(filep == NULL || filep->type != TRACE_BUFFER) {  // Check if file pointer is valid
		return -EINVAL;
	}
	if(filep->mode == O_READ) {  // Check if file is opened in read mode
		return -EINVAL;
	}
	if(buff == NULL) {  // Check if buffer is valid
		return -EINVAL;
	}
	if(count < 0) {
		return -EINVAL;
	}

	struct trace_buffer_info *trace_buf = filep->trace_buffer;
	if(trace_buf == NULL) {  // Check if trace buffer is valid
		return -EINVAL;
	}

	// if(is_valid_mem_range((unsigned long) buff, count, 0) != 0) {  // Check if buffer is valid
	// 	return -EBADMEM;
	// }

	int wcount = 0;
	while(wcount < count) {
		if(debug) printk("[TRACER WRITE] Running write loop with wcount %d\n", wcount);
		// if(trace_buf->write_ptr == trace_buf->read_ptr) {  // Check if trace buffer is full
		// 	break;
		// }
		*(trace_buf->buffer + trace_buf->write_ptr) = *(buff + wcount);  // Write to trace buffer
		wcount++;
		if((trace_buf->write_ptr + 1) % TRACE_BUFFER_MAX_SIZE == trace_buf->read_ptr) {
			trace_buf->is_full = 1;
			trace_buf->write_ptr = (trace_buf->write_ptr + 1) % TRACE_BUFFER_MAX_SIZE;
			break;
		}
		trace_buf->write_ptr = (trace_buf->write_ptr + 1) % TRACE_BUFFER_MAX_SIZE;  // Increment write pointer
	}
  return wcount;
}

int trace_buffer_read_os(struct file *filep, char *buff, u32 count)
{
	if(filep == NULL || filep->type != TRACE_BUFFER) {  // Check if file pointer is valid
		return -EINVAL;
	}
	if(filep->mode == O_WRITE) {  // Check if file is opened in write mode
		return -EINVAL;
	}
	if(buff == NULL) {  // Check if buffer is valid
		return -EINVAL;
	}
	if(count < 0) {
		return -EINVAL;
	}

	struct trace_buffer_info *trace_buf = filep->trace_buffer;
	if(trace_buf == NULL) {  // Check if trace buffer is valid
		return -EINVAL;
	}

	if(debug) printk("[TRACER READ MEM] Is valid mem range? %d\n", is_valid_mem_range((unsigned long) buff, count, 0));
	// if(is_valid_mem_range((unsigned long) buff, count, 1) != 0) {  // Check if buffer is valid
	// 	return -EBADMEM;
	// }

	int rcount = 0;
	while(rcount<count) {
		if(debug) printk("[TRACER READ] Running read loop with rcount %d\n", rcount);
		if(trace_buf->read_ptr == trace_buf->write_ptr && !trace_buf->is_full) {  // Check if trace buffer is empty
			break;
		}
		*(buff + rcount) = *(trace_buf->buffer + trace_buf->read_ptr);  // Read from trace buffer
		trace_buf->is_full = 0;
		trace_buf->read_ptr = (trace_buf->read_ptr + 1) % TRACE_BUFFER_MAX_SIZE;  // Increment read pointer
		rcount++;
	}
	return rcount;
}

int create_st_md_base(struct exec_context *current) {
	if(current == NULL) {
		return -EINVAL;
	}
	if(current->st_md_base == NULL) {
		current->st_md_base = (struct strace_head *)os_alloc(sizeof(struct strace_head));
		if(current->st_md_base == NULL || current->st_md_base == (void *)-1) {
			return -EINVAL;
		}
		// struct strace_head *st_head = current->st_md_base;
		current->st_md_base->count = 0;
		current->st_md_base->is_traced = 0;
		current->st_md_base->tracing_mode = -1;
		current->st_md_base->next = NULL;
		current->st_md_base->last = NULL;
	}
	return 0;
}

u64 get_arguments(u64 syscall)
{
	switch (syscall)
	{
	case 1:  // Exit
		return 1;
	case 2:  // Getpid
		return 0;
	case 4:  // Expand
		return 2;
	case 5:  // Shrink
		return 3;
	case 6:
		return 1;
	case 7:
		return 1;
	case 8:
		return 2;
	case 9:
		return 2;
	case 10:
		return 0;
	case 11:
		return 0;
	case 12:
		return 1;
	case 13:
		return 0;
	case 14:
		return 1;
	case 15:
		return 0;
	case 16:
		return 4;
	case 17:
		return 2;
	case 18:
		return 3;
	case 19:
		return 1;
	case 20:
		return 0;
	case 21:
		return 0;
	case 22:
		return 0;
	case 23:
		return 2;
	case 24:
		return 3;
	case 25:
		return 3;
	case 27:
		return 1;
	case 28:
		return 2;
	case 29:
		return 1;
	case 30:
		return 3;
	case 35:
		return 4;
	case 36:
		return 1;
	case 37:
		return 2;
	case 38:
		return 0;
	case 39:
		return 3;
	case 40:
		return 2;
	case 41:
		return 3;
	case 61:
		return 0;
	default:
		return -1;
	}
}

int perform_tracing(u64 syscall_num, u64 param1, u64 param2, u64 param3, u64 param4)
{
	if(syscall_num == 37 || syscall_num == 38 || syscall_num == 1) {
		return 0;
	}
	if(debug2) printk("[PERFORM TRACING] Called successfully for syscall number %d\n", syscall_num);
	struct exec_context *current = get_current_ctx();
	if(current == NULL) {
		return -EINVAL;
	}
	if(current->st_md_base == NULL) {
		return 0;
	}
	// if(debug2) printk("[PERFORM TRACING] Context and st_md_base are valid for syscall \n", syscall_num);
	
	struct strace_head *st_head = current->st_md_base;
	if(st_head->is_traced == 0) {
		// printk("HI");
		return 0;
	}
	if(st_head->tracing_mode == FILTERED_TRACING) {
		// if(debug2) printk("[PERFORM TRACING] Filtered tracing mode %d\n", syscall_num);
		int found = 0;
		struct strace_info *ptr = st_head->next;
		if(debug2) printk("[PERFORM TRACING] Searching for syscall number %d\n", syscall_num);
		while(ptr != NULL) {
			// if(ptr == 0x870001) ptr = 0x870000;
			if(debug2) printk("[PERFORM TRACING] Syscall number %d is traced %d %x %x %x\n", syscall_num, ptr->syscall_num, ptr, ptr->next, &ptr->syscall_num);
			if(ptr->syscall_num == syscall_num) {
				found = 1;
				break;
			}
			ptr = ptr->next;
		}
		if(!found) {
			if(debug2) printk("[PERFORM TRACING] Syscall number %d is not traced\n", syscall_num);
			return 0;
		}
	}
	// st_head->count++;
	int num_arg = get_arguments(syscall_num);
	if(num_arg == -1) {
		return -EINVAL;
	}
	// if(debug2) printk("[PERFORM TRACING] Number of arguments are valid\n");

	u64 param[] = {param1, param2, param3, param4};
	if(debug2) printk("[PERFORM TRACING] Attempting write to trace buffer for syscall\n");
	if(trace_buffer_write_os(current->files[st_head->strace_fd], (char *)&syscall_num, sizeof(u64)) != sizeof(u64) ) {
		return -EINVAL;
	}
	if(debug2) printk("[PERFORM TRACING] Syscall number written successfully\n");
	if(debug2) printk("[PERFORM TRACING] Attempting write to trace buffer for argument\n");
	for(int i = 0; i < num_arg; i++) {
		if(trace_buffer_write_os(current->files[st_head->strace_fd], (char *)&param[i], sizeof(u64)) != sizeof(u64)) {
			return -EINVAL;
		}
		if(debug2) printk("[PERFORM TRACING] Argument written successfully\n", i);
	}
	if(debug2) printk("[PERFORM TRACING] Exited successfully\n");
	current->st_md_base = st_head;
  return 0;
}

int sys_strace(struct exec_context *current, int syscall_num, int action)
{ 
	// printk("entered sys_strace\n");
	if(get_arguments(syscall_num) == -1) {
		return -EINVAL;
	}
	// printk("hi");
	if(current == NULL) {
		return -EINVAL;
	}
	// printk("hi");
	if(action != ADD_STRACE && action != REMOVE_STRACE) {
		return -EINVAL;
	}
	// printk("hi");
	if(current->st_md_base == NULL) {
		if(create_st_md_base(current) != 0) {
			return -EINVAL;
		}
	}
// printk("hi");
	struct strace_head *st_head = current->st_md_base;
// printk("hi");
	if(action == ADD_STRACE) {
		if(debug2) printk("[STRACE] Adding syscall number %d %d\n", syscall_num, current->st_md_base->count);
		if(current->st_md_base->count == STRACE_MAX) {
			return -EINVAL;
		}
		// Add a node to linked list with head as st_head with given syscall_num
		// printk("hi");
		struct strace_info *ptr = st_head->next;
		if(debug2) printk("[STRACE] Searching for syscall number %d\n", syscall_num);
		while(ptr != NULL) {
			if(ptr->syscall_num == syscall_num) {
				return -EINVAL;
			}
			ptr=ptr->next;
		}
		// printk("hi");
		if(debug2) printk("[STRACE] Adding syscall number %d\n", syscall_num);
		struct strace_info *new_strace = (struct strace_info *) os_alloc(sizeof(struct strace_info));
		if(new_strace == NULL || new_strace == (void *) -1) {
			return -EINVAL;
		}
		// printk("hi");
		new_strace->next = NULL;
		new_strace->syscall_num = syscall_num;
		if(debug2) printk("[STRACE] Last of st_head is %x\n", st_head->next);
		if(st_head->next == NULL){ st_head->next = new_strace; st_head->last=new_strace;}
		else {st_head->last->next = new_strace;st_head->last=new_strace;}
		if(debug2) printk("[STRACE] Head ka next hai %d\n", current->st_md_base->next);
		current->st_md_base = st_head;
		current->st_md_base->count++;

		if(debug2) printk("[STRACE] Added syscall number %d\n", syscall_num);
		// printk("hi");
	}
	else{
		// Remove a node from linked list with head as st_head with given syscall_num
		struct strace_info *ptr = st_head->next;
		struct strace_info *prev = NULL;
		int found = 0;
		while(ptr != NULL) {
			if(ptr->syscall_num == syscall_num) {
				found = 1;
				if(prev == NULL) {
					struct strace_head *prev2 = st_head;
					prev2->next = ptr->next;
					if(current->st_md_base->last == ptr) {
						current->st_md_base->last = NULL;
					}
					os_free(ptr, sizeof(struct strace_info));

					current->st_md_base->count--;
					break;
				}
				prev->next = ptr->next;
				if(current->st_md_base->last == ptr) {
					current->st_md_base->last = prev->next;
				}
				os_free(ptr, sizeof(struct strace_info));

				current->st_md_base->count--;
				break;
			}
			prev = ptr;
			ptr = ptr->next;
		}
		if(!found) {
			return -EINVAL;
		}
	}
	current->st_md_base = st_head;
	return 0;
}

int sys_read_strace(struct file *filep, char *buff, u64 count)
{
	if(filep == NULL) {
		return -EINVAL;
	}
	if(filep->type != TRACE_BUFFER) {
		return -EINVAL;
	}
	if(filep->mode != O_READ && filep->mode != O_RDWR) {
		return -EINVAL;
	}
	if(filep->fops == NULL) {
		return -EINVAL;
	}
	if(buff == NULL) {
		return -EINVAL;
	}
	if(count < 0) {
		return -EINVAL;
	}

	int read_offset = 0;
	struct exec_context *current = get_current_ctx();
	for(int cnt = 0; cnt < count; cnt++) {
		int ret = trace_buffer_read(filep, buff + read_offset, 8);
		if(ret == 0) break;
		u64 sysc_num = *(u64 *)(buff + read_offset);
		// current->st_md_base->count--;
		read_offset += 8;
		if(ret == -EINVAL) {  // Check valid read
			return -EINVAL;
		}
		if(debug2) printk("[READ STRACE] Read syscall number %d\n", sysc_num);
		int args = get_arguments(sysc_num);
		if(debug2) printk("[READ STRACE] Number of arguments are %d\n", args);
		if(args == -EINVAL) {  // Check valid syscall
			return -EINVAL;
		}

		for(int i = 0; i < args; i++) {
			int ret = trace_buffer_read(filep, buff + read_offset, 8);
			read_offset += 8;
			if(ret == -EINVAL) {  // Check valid read
				return -EINVAL;
			}
		}
	}
	return read_offset;
}

int sys_start_strace(struct exec_context *current, int fd, int tracing_mode)
{
	if(current == NULL) {
		return -EINVAL;
	}
	if(tracing_mode != FILTERED_TRACING && tracing_mode != FULL_TRACING) {
		return -EINVAL;
	}
	if(current->st_md_base == NULL) {
		if(create_st_md_base(current) != 0) {
			return -EINVAL;
		}
	}
	if(fd < 0 || fd >= MAX_OPEN_FILES) {
		return -EINVAL;
	} 
	// if(current->files[fd] == NULL) {
	// 	return -EINVAL;
	// }

	struct strace_head *st_head = current->st_md_base;
	if(debug2) printk("[START STRACE] Context and st_md_base are valid\n");
	// st_head->count = 0;
	st_head->is_traced = 1;
	st_head->strace_fd = fd;
	st_head->tracing_mode = tracing_mode;
	if(debug2) printk("[START STRACE] Tracing mode is %d\n", tracing_mode);
	// st_head->next = NULL;
	// st_head->last = NULL;
	if(debug2) printk("[START STRACE] REturning from start stract\n");
	st_head->count = 0;
	current->st_md_base = st_head;
	return 0;
}

int sys_end_strace(struct exec_context *current)
{
	if(current == NULL) {
		return -EINVAL;
	}
	if(current->st_md_base == NULL) {
		return -EINVAL;
	}
	struct strace_head *st_head = current->st_md_base;
	
	st_head->count = 0;
	st_head->is_traced = 0;
	st_head->last = NULL;
	while(st_head->next != NULL) {
		struct strace_info *temp = st_head->next->next;
		os_free(st_head->next, sizeof(struct strace_info));
		st_head->next = temp;
	}
	current->st_md_base = st_head;
	os_free(current->st_md_base, sizeof(struct strace_head));
	current->st_md_base = NULL;
	if(debug2) printk("[END STRACE] Returning from end strace %x\n", current->st_md_base->next);
	return 0;
}


///////////////////////////////////////////////////////////////////////////
//// 		Start of ftrace functionality 		      	      /////
///////////////////////////////////////////////////////////////////////////

int debug3 = 0;
u64 delimiterx = 0xf1e45a230b482c5b;

u64 *get_argument_pointer(struct user_regs *regs, int arg_num) {
	switch(arg_num) {
		case 0:
			return &(regs->rdi);
		case 1:
			return &(regs->rsi);
		case 2:
			return &(regs->rdx);
		case 3:
			return &(regs->rcx);
		case 4:
			return &(regs->r8);
		case 5:
			return &(regs->r9);
		default:
			return NULL;
	}
}

int create_ft_md_base(struct exec_context *current) {
	if(current == NULL) {
		return -EINVAL;
	}
	if(current->ft_md_base == NULL) {
		current->ft_md_base = (struct ftrace_head *)os_alloc(sizeof(struct ftrace_head));
		if(current->ft_md_base == NULL || current->ft_md_base == (void *)-1) {
			return -EINVAL;
		}
		// struct ftrace_head *st_head = current->ft_md_base;
		current->ft_md_base->count = 0;
		// current->ft_md_base->is_traced = 0;
		// st_head->strace_fd = -1;
		// st_head->next = NULL;
		// st_head->last = NULL;
	}
	return 0;
}

long do_ftrace(struct exec_context *ctx, unsigned long faddr, long action, long nargs, int fd_trace_buffer)
{
	if(ctx == NULL) {
		return -EINVAL;
	}
	if(faddr == 0) {
		return -EINVAL;
	}
	if(action != ADD_FTRACE && action != REMOVE_FTRACE && action != ENABLE_FTRACE && action != DISABLE_FTRACE && action != ENABLE_BACKTRACE && action != DISABLE_BACKTRACE) {
		return -EINVAL;
	}
	if(nargs < 0) {
		return -EINVAL;
	}
	if(ctx->files[fd_trace_buffer] == NULL) {
		return -EINVAL;
	}
	if(nargs >= MAX_ARGS) {
		return -EINVAL;
	}
	if(fd_trace_buffer < 0 || fd_trace_buffer >= MAX_OPEN_FILES) {
		return -EINVAL;
	}

	if(debug3) printk("[FTRACE] 32 bit Sample at faddr %x %d is %x\n", faddr, nargs, *(unsigned long *)faddr);
	if(action == ADD_FTRACE) {
		if(debug3) printk("[FTRACE] Adding ftrace for faddr %x\n", faddr);
		if(ctx->ft_md_base == NULL) {  // Allocate ft_md_base if not found
			if(create_ft_md_base(ctx) == -EINVAL) {
				return -EINVAL;
			}
		}
		if(ctx->ft_md_base->count == FTRACE_MAX) {  // Check if possible to add ftrace
			return -EINVAL;
		}

		if(debug3) printk("[FTRACE] Checking ftrace if found for %x\n", faddr);

		// Check if function is traced
		int func_found = 0;
		struct ftrace_info *ftrace_ptr = ctx->ft_md_base->next;
		while(ftrace_ptr != NULL) {
			if(ftrace_ptr->faddr == faddr) {
				func_found = 1;
				break;
			}
			ftrace_ptr = ftrace_ptr->next;
		}
		if(func_found) { // Function already traced
			return -EINVAL;
		}

		if(debug3) printk("[FTRACE] Not found. Adding ftrace for faddr %x\n", faddr);

		// Allocate new ftrace_info
		struct ftrace_info *ftrace_inf = (struct ftrace_info *) os_alloc(sizeof(struct ftrace_info));
		if(ftrace_inf == NULL || ftrace_inf == (void *) -1) {
			return -EINVAL;
		} 
		ftrace_inf->faddr = faddr;
		ftrace_inf->num_args = nargs;
		ftrace_inf->fd = fd_trace_buffer;
		ftrace_inf->capture_backtrace = 0;
		ftrace_inf->next = NULL;

		if(ctx->ft_md_base->last == NULL) {
			ctx->ft_md_base->next = ftrace_inf;
			ctx->ft_md_base->last = ftrace_inf; 
		}
		else {
			ctx->ft_md_base->last->next = ftrace_inf;
			ctx->ft_md_base->last = ftrace_inf;
		}
		ctx->ft_md_base->count++;
		if(debug3) printk("[FTRACE] Added ftrace for faddr %x\n", faddr);
		return 0;
	}
	else if(action == REMOVE_FTRACE) {
		if(ctx->ft_md_base == NULL) {  // Check ft_md_base if exists
			return -EINVAL;
		}
		if(ctx->ft_md_base == NULL) {
			return -EINVAL;
		}
		// Check if function is traced
		// struct ftrace_info *ftrace_ptr = ctx->ft_md_base->next;
		if(ctx->ft_md_base->next->faddr == faddr) {
			struct ftrace_info *ptr = ctx->ft_md_base->next;
			if(*(u8 *)(faddr) == INV_OPCODE && *((u8 *)faddr + 1) == INV_OPCODE && *(u8 *)((u8 *)faddr + 2) == INV_OPCODE && *(u8 *)((u8 *)faddr + 3) == INV_OPCODE) {
				*(u32 *)faddr = *(u32 *)ptr->code_backup;
				// int ret = do_ftrace(ctx, faddr, DISABLE_FTRACE, nargs, fd_trace_buffer);
				// printk("MAI NADAR SE TUT CHUKA HOON\n");
			}
			ctx->ft_md_base->next = ctx->ft_md_base->next->next;
			if(ctx->ft_md_base->last == ptr) {
				ctx->ft_md_base->last = ptr->next;
			}
			os_free(ptr, sizeof(struct ftrace_info));
			ctx->ft_md_base->count--;
		}
		else {
			int func_found = 0;
			struct ftrace_info *prev = NULL;
			struct ftrace_info *cur = ctx->ft_md_base->next;
			while(cur != NULL) {
				if(cur->faddr == faddr) {
					if(*(u8 *)(faddr) == INV_OPCODE && *((u8 *)faddr + 1) == INV_OPCODE && *(u8 *)((u8 *)faddr + 2) == INV_OPCODE && *(u8 *)((u8 *)faddr + 3) == INV_OPCODE) {
						*(u32 *)faddr = *(u32 *)cur->code_backup;
						// int ret = do_ftrace(ctx, faddr, DISABLE_FTRACE, nargs, fd_trace_buffer);
						// printk("MAI NADAR SE TUT CHUKA HOON\n");
					}
					prev->next = cur->next;
					if(ctx->ft_md_base->last == cur) {
						ctx->ft_md_base->last = prev;
					}
					os_free(cur, sizeof(struct ftrace_info));
					func_found = 1;
					break;
				}
				prev = cur;
				cur = cur->next;
			}
			if(!func_found) { // Function not traced
				return -EINVAL;
			}
			ctx->ft_md_base->count--;
			return 0;
		}
	}
	else if(action == ENABLE_FTRACE) {
		if(ctx->ft_md_base == NULL) {  // Check ft_md_base if exists
			return -EINVAL;
		}
		
		// Check if function is traced
		int func_found = 0;
		struct ftrace_info *ft_info = ctx->ft_md_base->next;
		while(ft_info != NULL) {
			if(ft_info->faddr == faddr) {
				func_found = 1;
				break;
			}
			ft_info = ft_info->next;
		}
		if(!func_found) { // Function not traced
			return -EINVAL;
		}

		if(*(u8 *)(ft_info->faddr) == INV_OPCODE && *((u8 *)ft_info->faddr + 1) == INV_OPCODE && *(u8 *)((u8 *)ft_info->faddr + 2) == INV_OPCODE && *(u8 *)((u8 *)ft_info->faddr + 3) == INV_OPCODE) {
			// int ret = do_ftrace(ctx, faddr, DISABLE_FTRACE, nargs, fd_trace_buffer);
			// printk("MAI NADAR SE TUT CHUKA HOON\n");
			return 0;
		}

		*(u32 *)ft_info->code_backup = *(u32 *)ft_info->faddr;
		*(u8 *)((u8 *)(ft_info->faddr)) = INV_OPCODE;
		*(u8 *)(((u8 *)(ft_info->faddr)) + 1) = INV_OPCODE;
		*(u8 *)(((u8 *)(ft_info->faddr)) + 2) = INV_OPCODE;
		*(u8 *)(((u8 *)(ft_info->faddr)) + 3) = INV_OPCODE;

		return 0;
	}
	else if(action == DISABLE_FTRACE) {
		if(ctx->ft_md_base == NULL) {  // Check ft_md_base if exists
			return -EINVAL;
		}
		
		// Check if function is traced
		int func_found = 0;
		struct ftrace_info *ft_info = ctx->ft_md_base->next;
		while(ft_info != NULL) {
			if(ft_info->faddr == faddr) {
				func_found = 1;
				break;
			}
			ft_info = ft_info->next;
		}
		if(!func_found) { // Function not traced
			return -EINVAL;
		}

		if(debug3) printk("[FTRACE] Disabling ftrace for faddr %x with %x\n", *(u32 *)faddr, *(u32 *)ft_info->code_backup);
		*(u32 *)ft_info->faddr = *(u32 *)ft_info->code_backup;
		// *(u8 *)ft_info->faddr = INV_OPCODE;
		// *(u8 *)(ft_info->faddr + 1) = INV_OPCODE;
		// *(u8 *)(ft_info->faddr + 2) = INV_OPCODE;
		// *(u8 *)(ft_info->faddr + 3) = INV_OPCODE;

		return 0;
	}
	else if(action == ENABLE_BACKTRACE) {	
		if(ctx->ft_md_base == NULL) {  // Check ft_md_base if exists
			return -EINVAL;
		}
		
		// Check if function is traced
		int func_found = 0;
		struct ftrace_info *ft_info = ctx->ft_md_base->next;
		while(ft_info != NULL) {
			if(ft_info->faddr == faddr) {
				func_found = 1;
				break;
			}
			ft_info = ft_info->next;
		}
		if(!func_found) { // Function not traced
			return -EINVAL;
		}

		if(*(u8 *)(ft_info->faddr) != INV_OPCODE || *(u8 *)((u8 *)ft_info->faddr + 1) != INV_OPCODE || *(u8 *)((u8 *)ft_info->faddr + 2) != INV_OPCODE || *(u8 *)((u8 *)ft_info->faddr + 3) != INV_OPCODE) {
			int ret = do_ftrace(ctx, faddr, ENABLE_FTRACE, nargs, fd_trace_buffer);
			if(ret) {
				return -EINVAL;
			}
		}
		// if(ft_info->capture_backtrace == 1) {
		// 	return -EINVAL;
		// }

		ft_info->capture_backtrace = 1;  // Enable backtracing
		return 0;
	}
	else if(action == DISABLE_BACKTRACE) {
		if(ctx->ft_md_base == NULL) {  // Check ft_md_base if exists
			return -EINVAL;
		}
		
		// Check if function is traced
		int func_found = 0;
		struct ftrace_info *ft_info = ctx->ft_md_base->next;
		while(ft_info != NULL) {
			if(ft_info->faddr == faddr) {
				func_found = 1;
				break;
			}
			ft_info = ft_info->next;
		}
		if(!func_found) { // Function not traced
			return -EINVAL;
		}

		// if(ft_info->capture_backtrace == 0) {
		// 	return -EINVAL;
		// }

		ft_info->capture_backtrace = 0;  // Disable backtracing

		if(*(u8 *)(ft_info->faddr) == INV_OPCODE && *(u8 *)((u8 *)ft_info->faddr + 1) == INV_OPCODE && *(u8 *)((u8 *)ft_info->faddr + 2) == INV_OPCODE && *(u8 *)((u8 *)ft_info->faddr + 3) == INV_OPCODE) {
			int ret = do_ftrace(ctx, faddr, DISABLE_FTRACE, nargs, fd_trace_buffer);
			if(ret) {
				return -EINVAL;
			}
		}

		return 0;
	}
  return 0;
}

//Fault handler
long handle_ftrace_fault(struct user_regs *regs)
{
	if(regs == NULL) {
		return -EINVAL;
	}
	struct exec_context *ctx = get_current_ctx();
	if(ctx == NULL) {
		return -EINVAL;
	}
	if(ctx->ft_md_base == NULL) {
		return -EINVAL;
	}
	if(ctx->ft_md_base->count == 0) {
		return -EINVAL;
	}
	int count=0;
	unsigned long faddr = regs->entry_rip;
	if(debug3) printk("[FTRACE FAULT] Fault at faddr %x\n", faddr);

	// Find function in ft_md_base
	struct ftrace_info *ft_info = ctx->ft_md_base->next;
	while(ft_info != NULL) {
		if(ft_info->faddr == faddr) {
			break;
		}
		ft_info = ft_info->next;
	}
	if(ft_info == NULL) {
		return -EINVAL;
	}
  count+=8;
	trace_buffer_write_os(ctx->files[ft_info->fd], (char *)&faddr, sizeof(u64)); 
	if(debug3) printk("[FTRACE FAULT] Written faddr %x\n", faddr);
	for(int i = 0; i < ft_info->num_args; i++) {
		u64 *arg_ptr = get_argument_pointer(regs, i);
		if(arg_ptr == NULL) {
			return -EINVAL;
		}
		count+=8;
		trace_buffer_write_os(ctx->files[ft_info->fd], (char *)arg_ptr, sizeof(u64));
		if(debug3) printk("[FTRACE FAULT] Written argument %d %x\n", i, *arg_ptr);
	}
	
	//Push rbp onto stack and set rbp = rsp and increment rip by 4 bytes
	regs->entry_rsp -= 8;
	*(u64 *)regs->entry_rsp = regs->rbp;
	regs->rbp = regs->entry_rsp;
	regs->entry_rip += 4;

	if(debug3) printk("[FTRACE FAULT] Pushed rbp %x\n", regs->rbp);

	if(ft_info->capture_backtrace == 1) {
		// if((u64 *) regs->entry_rsp != END_ADDR) {
		// 	trace_buffer_write_os(ctx->files[ft_info->fd], (char *)&regs->entry_rsp, sizeof(u64));
		// 	if(debug3) printk("[FTRACE FAULT] Written rsp %x\n", regs->entry_rsp);
		// } 
		if(debug3)printk("inside backtrace %d\n", count);
		u64 rbp = (u64 )regs->rbp;
		// u64 *rbp_prev = (u64 *)*rbp;
		// u64 *rip = (u64 *)*(rbp + 8);
	
    trace_buffer_write_os(ctx->files[ft_info->fd], (char *)&faddr, sizeof(u64));
		while(*(u64 *)(rbp+8) != END_ADDR) {
		  u64  rip= *(u64 *)(rbp+8);
			count+=8;

			if(debug3) printk("[BACKTRACE] Backtrace is %x\n", rip);
			trace_buffer_write_os(ctx->files[ft_info->fd], (char *)&rip, sizeof(u64));
      rbp = *(u64 *)rbp;
			// count++;
		}
	}

  if(debug3) printk("[FTRACE FAULT] Returning from fault handler %d\n",count);
	u64 delimiter = delimiterx;
	trace_buffer_write_os(ctx->files[ft_info->fd], (char *)&delimiter, sizeof(u64));
  if(debug3) printk("[FTRACE FAULT] Written delimiter\n");
  return 0;
}


int sys_read_ftrace(struct file *filep, char *buff, u64 count)
{
	if(filep == NULL) {
		return -EINVAL;
	}
	if(filep->type != TRACE_BUFFER) {
		return -EINVAL;
	}
	if(filep->mode != O_READ && filep->mode != O_RDWR) {
		return -EINVAL;
	}
	if(filep->fops == NULL) {
		return -EINVAL;
	}
	if(buff == NULL) {
		return -EINVAL;
	}
	if(count < 0) {
		return -EINVAL;
	}
	if(debug3) printk("[READ FTRACE] Reading ftrace\n");
	int read_offset = 0;
	int ret_offset = 0;
	struct exec_context *current = get_current_ctx();
	for(int cnt = 0; cnt < count; cnt++) {
		if(debug3) printk("[READ FTRACE] Reading ftrace2\n");
		u64 read_val = 0;
		int ret = trace_buffer_read_os(filep, (char *)(&read_val), 8);

		if(debug3) printk("[READ FTRACE] Read return %d\n", ret);
		if(ret == 0)   break;
		// if(ret !=8) return -EINVAL;
		if(debug3) printk("[READ FTRACE] Read function addr %x\n", read_val);
		u64 faddr = read_val;
		// if(debug3) printk("[READ FTRACE] Read faddr %d\n", faddr);
		
		// read_offset += 8;
		// ret_offset += 8;
		do {
			if(debug3) printk("[READ FTRACE] Read argument %x\n", read_val);
			*(unsigned long *)(buff + read_offset) = read_val;
			read_offset += 8;
			ret_offset += 8;

			int ret = trace_buffer_read_os(filep, (char *)&read_val, 8);
			if(ret != 8) return -EINVAL;
		}
		while(read_val != delimiterx);
	}
	if(debug3) printk("[READ FTRACE] Read bytes %d\n", read_offset);
	// read_offset+=8;
	// ret_offset -= 8;		
	// int args = get_arguments(faddr);
	// if(debug3) printk("[READ FTRACE] Number of arguments are %d\n", args);
	// if(args == -EINVAL) {  // Check valid syscall
	// 	return -EINVAL;
	// }
	if(debug3) {
		printk("[READ FTRACE] Buffer Output\n");
		for(int i = 0; i < read_offset; i+=8) {
			printk("%x\n", *(unsigned long *)(buff + i));
		}
		// printk("\n");
	}
  return ret_offset;
}



