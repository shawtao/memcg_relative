#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/unistd.h>
#include <linux/sched.h>
#include <linux/syscalls.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/fdtable.h>
#include <linux/uaccess.h> 
#include <linux/kallsyms.h> 
#include <linux/rtc.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/moduleparam.h>
#include <linux/file.h>
#include <linux/cgroup-defs.h>
#include <linux/memcontrol.h>
#include <linux/proc_fs.h>
#include <linux/fsnotify.h>
#include <linux/namei.h>

#include "containerproc.h"
#include "internal.h"

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("shawtao"); 
MODULE_DESCRIPTION("hook sys_open"); 


static char *pidname;
unsigned long sys_call_table_addr = 0; 
long g_oldcr0 = 0;                  //save address of cr0
typedef asmlinkage long (*orig_open)(const char __user *filename, int flags, unsigned short mode);
typedef asmlinkage long(*orig_read)(int fd,void *buf,size_t count);
orig_open g_old_open = NULL;
orig_read g_old_read = NULL;

struct filename *(*orig_getname_kernel)(const char * filename);
void (*orig_putname)(struct filename *name);
struct file *(*orig_do_filp_open)(int dfd,struct filename *pathname,const struct open_flags *op);

 
 module_param(pidname,charp,0644);
 MODULE_PARM_DESC(pidname,"A string variable");
 
unsigned int close_cr(void){
 
    unsigned int cr0 = 0;
    unsigned int ret;
    asm volatile("movq %%cr0,%%rax":"=a"(cr0));
    ret = cr0;
    cr0 &= 0xfffeffff;
    asm volatile("movq %%rax,%%cr0"::"a"(cr0));
    return ret;
 
}
 
 
 
void open_cr(unsigned int oldval){
 
    asm volatile("movq %%rax,%%cr0"::"a"(oldval));
 
}
 
asmlinkage long hooked_read(int fd,void *buffer,size_t count){
	  struct file *file;
	  char *father_pidname = NULL;
	  file = fget(fd);
	  father_pidname = (current->parent)->comm;
	  if((strcmp(current->comm,"bash")==0) && (strcmp(father_pidname,"docker-run")==0)){
		   //printk("Task name:%s,task ID:%d,Parentpid:%s\n",current->comm,current->pid,father_pidname);
		   return g_old_read(fd,buffer,count);
	  }
	  else{
		   return g_old_read(fd,buffer,count);
	  }
}		   
 
int build_open_flags(int flags, umode_t mode, struct open_flags *op)
{
	int lookup_flags = 0;
	int acc_mode = ACC_MODE(flags);

	flags &= VALID_OPEN_FLAGS;

	if (flags & (O_CREAT | __O_TMPFILE))
		op->mode = (mode & S_IALLUGO) | S_IFREG;
	else
		op->mode = 0;

	flags &= ~FMODE_NONOTIFY & ~O_CLOEXEC;
	
	if (flags & __O_SYNC)
		flags |= O_DSYNC;

	if (flags & __O_TMPFILE) {
		if ((flags & O_TMPFILE_MASK) != O_TMPFILE)
			return -EINVAL;
		if (!(acc_mode & MAY_WRITE))
			return -EINVAL;
	} else if (flags & O_PATH) {
		flags &= O_DIRECTORY | O_NOFOLLOW | O_PATH;
		acc_mode = 0;
	}

	op->open_flag = flags;

	if (flags & O_TRUNC)
		acc_mode |= MAY_WRITE;

	if (flags & O_APPEND)
		acc_mode |= MAY_APPEND;

	op->acc_mode = acc_mode;

	op->intent = flags & O_PATH ? 0 : LOOKUP_OPEN;

	if (flags & O_CREAT) {
		op->intent |= LOOKUP_CREATE;
		if (flags & O_EXCL)
			op->intent |= LOOKUP_EXCL;
	}

	if (flags & O_DIRECTORY)
		lookup_flags |= LOOKUP_DIRECTORY;
	if (!(flags & O_NOFOLLOW))
		lookup_flags |= LOOKUP_FOLLOW;
	op->lookup_flags = lookup_flags;
	return 0;
}

long my_do_sys_open(int dfd,const char *filename,int flags,unsigned short mode){
	orig_getname_kernel = (void *)kallsyms_lookup_name("getname_kernel");
	orig_putname = (void *)kallsyms_lookup_name("putname");
	orig_do_filp_open = (void *)kallsyms_lookup_name("do_filp_open");
	
	struct open_flags op;
	int fd = build_open_flags(flags,mode,&op);
	struct filename *tmp;
	
	if(fd)
			return fd;
	
	tmp = orig_getname_kernel(filename);
	if(IS_ERR(tmp))
			return PTR_ERR(tmp);
	
	fd = get_unused_fd_flags(flags);
	if(fd >= 0) {
			struct file *f = orig_do_filp_open(dfd,tmp,&op);
			if(IS_ERR(f)) {
					put_unused_fd(fd);
					fd = PTR_ERR(f);
			}else{
					fsnotify_open(f);
					fd_install(fd,f);
			}
	}
	orig_putname(tmp);
	return fd;
}
 
asmlinkage long my_sys_open(const char *filename,int flags,unsigned short mode){
	long ret;
	if(force_o_largefile())
		flags |= O_LARGEFILE;
	return my_do_sys_open(AT_FDCWD,filename,flags,mode);
}

	
asmlinkage long hooked_open(const char __user * filename, int flags, unsigned short mode){
    struct file *fp;
    mm_segment_t fs;
    loff_t pos;
    int filename_size;
    char * temp_str = NULL; 
    char * file_str = NULL;
    char * father_pidname = NULL;
    char * grandfather_pidname = NULL;
    struct task_struct *p,*container;
    struct timex  txc;
    struct rtc_time tm;
    char __user * userfilename = NULL;
 
    filename_size = 1024;  
    
    p= current->parent;
    father_pidname = p->comm;
    container = p->parent;
    grandfather_pidname = container->comm;
    
    
if((strcmp(father_pidname,"bash")) == 0 && (strcmp(grandfather_pidname,"docker-run") == 0)){
	
    temp_str = kmalloc(filename_size , GFP_KERNEL);
    if(temp_str == NULL)
    {       
        goto end;
    }
 
    file_str = kmalloc(filename_size , GFP_KERNEL);
    if(file_str == NULL)
    {       
        goto end;
    }
    
    /*newfilename = kmalloc(filename_size,GFP_KERNEL);
    if(newfilename == NULL)
    {
		goto end;
	}*/
    
    memset(temp_str, 0, filename_size );
    if (temp_str != NULL) {
        //copy_from_user(exec_str,filename, strnlen_user(filename,1000));
        if(copy_from_user(temp_str,filename, strnlen_user(filename,1000)) != 0)
            goto end;
    }
    
    if(strcmp(temp_str,"/proc/meminfo")==0){
		
		printk("intercept open ! Task name:%s,task ID:%d,Parentpid:%s\n filename:%s",p->comm,p->pid,grandfather_pidname,temp_str); 
		
		int err;
		err = create_container_proc();
		//printk("err = %d",err);
		//if(err != 0) 
		//	goto end;
		
		char *newfilename = "/proc/containermeminfo";  
		
        return my_sys_open(newfilename,flags,mode);     
     }
     
end:
    if(temp_str != NULL)
    {   
        kfree(temp_str);
    }
    if(file_str != NULL)
    {   
        kfree(file_str);
    }
    
    return g_old_open(filename,flags,mode);
}
else{
	return g_old_open(filename,flags,mode);
}

}
 
 
static int obtain_sys_call_table_addr(unsigned long * sys_call_table_addr) { 
    int ret = 1; 
    unsigned long temp_sys_call_table_addr;
    //利用内核导出的kallsyms_lookup_name()来获得sys_call_table地址
    temp_sys_call_table_addr = kallsyms_lookup_name("sys_call_table"); 
    if (0 == sys_call_table_addr) { 
        ret = -1; 
        goto cleanup; 
    } 
    printk("Found sys_call_table: %p", (void *) temp_sys_call_table_addr); 
    *sys_call_table_addr = temp_sys_call_table_addr; 
cleanup: 
    return ret; 
} 
 
// initialize the module 
static int hooked_init(void) {
    printk("+ Loading hook_open module\n"); 
    printk("pidname is %s\n",pidname);
    int ret = -1; 
    ret = obtain_sys_call_table_addr(&sys_call_table_addr);    
    if(ret != 1){ 
        printk("- unable to locate sys_call_table\n"); 
        return 0; 
    } 
    if(((unsigned long * ) (sys_call_table_addr))[__NR_close]!= (unsigned long)sys_close){

        printk("Incorrect sys_call_table address!\n");
 
        return 1;
 
    }
    g_old_open = ((unsigned long * ) (sys_call_table_addr))[__NR_open];  
    g_old_read =((unsigned long *  ) (sys_call_table_addr))[__NR_read];
    // unprotect sys_call_table memory page 
    g_oldcr0 = close_cr();       
    printk("+ unprotected kernel memory page containing sys_call_table\n");  
    // now overwrite the __NR_uname entry with address to our uname 
    ((unsigned long * ) (sys_call_table_addr))[__NR_open]= (unsigned long) hooked_open; 
    ((unsigned long * ) (sys_call_table_addr))[__NR_read]= (unsigned long) hooked_read;
    open_cr(g_oldcr0);    
    printk("+ sys_execve hooked!\n"); 
    return 0; 
} 
static void hooked_exit(void) { 
    if(g_old_open != NULL) { 
        // restore sys_call_table to original state 
        g_oldcr0 = close_cr();
 
        ((unsigned long * ) (sys_call_table_addr))[__NR_open] = (unsigned long) g_old_open; 
        ((unsigned long * ) (sys_call_table_addr))[__NR_read] = (unsigned long) g_old_read;
        // reprotect page 
        open_cr(g_oldcr0);
    } 
    
    remove_proc_entry("containermeminfo",NULL);
    printk("+ Unloading hook_execve module\n"); 
} 

module_init(hooked_init); 
module_exit(hooked_exit);

