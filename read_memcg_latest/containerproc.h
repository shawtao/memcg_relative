#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "read_memcg.h"


static int memcg_proc_show(struct seq_file *m,void *v)
{
	struct list_head *pos;
    struct docker_tasks *dtsk;
	void (*orig_show_map_vma)(struct seq_file *m, struct vm_area_struct *vma) = kallsyms_lookup_name("show_map_vma");

    list_for_each(pos, &(all_docker_tasks->list)) {
        dtsk = list_entry(pos, struct docker_tasks, list);
        seq_printf(m,"\nExamining vma's for pid=%d, command=%s, total_rss = %u, not_count_rss = %u, memcg_path = %s\n",
			dtsk->tsk->pid, dtsk->tsk->comm, dtsk->task_rss, dtsk->not_count_rss, dtsk->memcg_path);
        seq_printf(m,"\n Pages has different mem_cgroup with task's mem_cgroup: \n");
        
		struct list_head *page_stats_pos;
        struct page_stats *pgs;
        
		list_for_each(page_stats_pos, &(dtsk->memcg_notequal)) {
			char pagetype[100] = " ";
			pgs = list_entry(page_stats_pos, struct page_stats, list);
			PrintPageType(pgs->page, pagetype);
			seq_printf(m,"page frame struct is @ %p PageType:%s",pgs->page,pagetype);
			seq_puts(m,"vma: ");
			orig_show_map_vma(m,pgs->vma);
			
			if(pgs->page_memcg != NULL) {
				pr_info("Not equal and not null. Task_memcg is @ %p, Page_memcg is @ %p", dtsk->task_memcg, pgs->page_memcg);
				/*if(strcmp(dtsk->tsk->comm, "mmap") == 0 && count < 50){
				char *buf;
				buf = kmalloc(PATH_MAX, GFP_KERNEL);
        		if(!buf){
            		pr_info("Buf alloc failed!\n");
            		return -1;
        		}

				if(MemcgPath(pgs->page_memcg, buf, PATH_MAX) < 0){
            		pr_info("Get MemcgPath failed!\n");
            		kfree(buf);
					continue;
        		}
				seq_puts(m, "Page_Memcg: ");
				seq_puts(m, buf);
				seq_puts(m,"\n");
				//seq_puts(m, "Page_Memcg is not null ");
				//seq_puts(m,"\n");
				kfree(buf);
				count++;
				} */
			}
        }
    }
	return 0;
}

static int memcg_proc_open(struct inode *inode,struct file *file)
{
		return single_open(file,memcg_proc_show,NULL);
}

static struct file_operations memcg_proc_fops = {
		.owner   = THIS_MODULE,
		.open    = memcg_proc_open,
		.release = single_release,
		.read    = seq_read,
		.llseek  = seq_lseek,
};

static int create_memcg_proc(void)
{
		struct proc_dir_entry *file;
		file = proc_create("MemcgInfo", 0644, NULL, &memcg_proc_fops);
		if(!file)
			return -ENOMEM;
		return 0;
}


