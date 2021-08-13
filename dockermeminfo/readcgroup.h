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
#include <stdarg.h>


MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("shawtao"); 
MODULE_DESCRIPTION("readcgroup"); 

static int collect_subsystems(pid_t pidnum)
{
	struct file *fp;
	mm_segment_t fs;
	loff_t pos;
	//pid_t pid;
	char path[256];
	int i = 0;
	int j = 0;
	int length = 0;
	char temp;
	char buf[20][256];
	
	//pid = p->pid;
	printk("inside collect_subsystems!");
	sprintf(path,"/proc/%d/cgroup",pidnum);
	printk("path:%s",path);
	fp = filp_open("proc/1/cgroup",O_RDONLY,0);
	if(IS_ERR(fp)){
		int err = PTR_ERR(fp);
		printk("open file error!!  err num:%d",err);
		return -1;
	}
	
	fs =get_fs();
	set_fs(KERNEL_DS);
	pos = 0;
	printk("file open success!");
	do
	{
		if(vfs_read(fp,&temp,sizeof(temp),&pos) == -EFAULT) break;
		if(temp == '\n'){
			buf[j][i] = '\0';
			printk("read :%s",buf[j]);
			i = 0;
			j++;
			if(j == 11) break;
		}
		else{
			buf[j][i++] = temp;
		}
	}while(1);
	length = j;
	for(j = 0;j<length;j++){
		printk("readis : %s",buf[j]);
	}  
	filp_close(fp,NULL);
	set_fs(fs);
	return 0;
}
	
	
		
