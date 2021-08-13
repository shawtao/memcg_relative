#include <linux/module.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/mman.h>
#include <linux/mmzone.h>
#include <linux/seq_file.h>
#include <linux/swap.h>
#include <linux/vmstat.h>
#include <linux/atomic.h>
#include <linux/vmalloc.h>
#include <linux/types.h>
#include <linux/memcontrol.h>
#include <linux/cgroup.h>
#include <linux/cgroup-defs.h>
#include <linux/page_counter.h>
#include <linux/kallsyms.h>

//static char *str = "hello!";

unsigned long memtotal;
unsigned long memfree;
unsigned long available;
unsigned long buffers;
unsigned long cached;
unsigned long swapcached;
unsigned long active;
unsigned long inactive;
unsigned long active_anon;
unsigned long inactive_anon;
unsigned long active_file;
unsigned long inactive_file;
unsigned long swaptotal;
unsigned long swapfree;
unsigned long shemem;

unsigned int memcg1_stats[] = {
	MEMCG_CACHE,
	MEMCG_RSS,
	MEMCG_RSS_HUGE,
	NR_SHMEM,
	NR_FILE_MAPPED,
	NR_FILE_DIRTY,
	NR_WRITEBACK,
	MEMCG_SWAP,
};

static const char *const mem_cgroup_lru_names[] = {
	"inactive_anon",
	"active_anon",
	"inactive_file",
	"active_file",
	"unevictable",
};

unsigned long (*orig_mem_cgroup_nr_lru_pages)(struct mem_cgroup *memcg,unsigned int lru_mask);
struct mem_cgroup *(*orig_mem_cgroup_iter)(struct mem_cgroup *,struct mem_cgroup *,struct mem_cgroup_reclaim_cookie *);
int (*orig_num_to_str)(char *buf,int size,unsigned long long num);
struct mem_cgroup *root_mem_cgroup __read_mostly;
//下面是从cgroup获取信息部分
//


struct mem_cgroup *getcgroup(struct task_struct *p){
	struct cgroup *cg = NULL;
	struct css_set *cssset = NULL;
	//struct cg_group_link cglink = NULL;
	struct cgroup_subsys_state *cgsysstate = NULL;
	struct cgroup_subsys_state *cgsysstate1 = NULL;
	struct cgroup_subsys *cgsys = NULL;
	int i = 0;
	
	cssset = p->cgroups;
	
	do
	{   if(i == CGROUP_SUBSYS_COUNT) break;
		cgsysstate = cssset->subsys[i];
		cgsys = cgsysstate->ss;
		printk("cgsys id : %d",cgsys->id);
		if(cgsys->id == 4){
			printk("subsys:%s!",cgsys->name);
			cgsysstate1 = cgsysstate;
			break;
		}
		else{
			i++;
		}
	}while(1);
	
	 struct mem_cgroup *mcg = NULL;
	
	if((mcg = container_of(cgsysstate1,struct mem_cgroup,css))==NULL){
		printk("get memcg error!");
		return NULL;
	   }
		
	return mcg;
}

#define for_each_mem_cgroup_tree(iter, root)		\
		orig_mem_cgroup_iter = (void *)kallsyms_lookup_name("mem_cgroup_iter");		\
		for (iter = orig_mem_cgroup_iter(root, NULL, NULL);	\
			iter != NULL;				\
			iter = orig_mem_cgroup_iter(root, iter, NULL))

static void tree_stat(struct mem_cgroup *memcg, unsigned long *stat)
{
	struct mem_cgroup *iter;
	int i;

	memset(stat, 0, sizeof(*stat) * MEMCG_NR_STAT);

	for_each_mem_cgroup_tree(iter, memcg) {
		for (i = 0; i < MEMCG_NR_STAT; i++)
			stat[i] += memcg_page_state(iter, i);
	}
}



static bool mem_cgroup_is_root(struct mem_cgroup *memcg)
{
	return (memcg  == root_mem_cgroup);
}

static unsigned long mem_cgroup_usage(struct mem_cgroup *memcg, bool swap)
{
	unsigned long val = 0;

	if (mem_cgroup_is_root(memcg)) {
		struct mem_cgroup *iter;

		for_each_mem_cgroup_tree(iter, memcg) {
			val += memcg_page_state(iter, MEMCG_CACHE);
			val += memcg_page_state(iter, MEMCG_RSS);
			if (swap)
				val += memcg_page_state(iter, MEMCG_SWAP);
		}
	} else {
		if (!swap)
			val = page_counter_read(&memcg->memory);
		else
			val = page_counter_read(&memcg->memsw);
	}
	return val;
}
		
static u64 get_usage_in_bytes(struct mem_cgroup *mcg){
	   struct page_counter *counter;
	   counter = &mcg->memory;
	   return (u64)mem_cgroup_usage(mcg,false) * PAGE_SIZE;
}

static u64 get_limit_in_bytes(struct mem_cgroup *mcg){
		struct page_counter *counter;
		counter = &mcg->memory;
		return (u64)counter->limit * PAGE_SIZE;
}

static u64 get_memsw_limit_in_bytes(struct mem_cgroup *mcg){
		struct page_counter *counter;
		counter = &mcg->memsw;
		return (u64)counter->limit * PAGE_SIZE;
}

static unsigned long get_cache(struct mem_cgroup *mcg){
		return memcg_page_state(mcg,memcg1_stats[0]) * PAGE_SIZE;
}

static unsigned long get_memsw_usage_in_bytes(struct mem_cgroup *mcg){
		struct page_counter *counter;
		counter = &mcg->memsw;
		return (u64)mem_cgroup_usage(mcg,true) * PAGE_SIZE;
}

 static unsigned long get_total_shmes(struct mem_cgroup *memcg){
		unsigned long memory,memsw;
		struct mem_cgroup *mi;
		unsigned long val = 0;
		memory = memsw = PAGE_COUNTER_MAX;
		for(mi = memcg;mi;mi = parent_mem_cgroup(mi)){
			memory = min(memory,mi->memory.limit);
			memsw = min(memsw,mi->memsw.limit);
		}
		for_each_mem_cgroup_tree(mi,memcg)
			val += memcg_page_state(mi,memcg1_stats[3]) * PAGE_SIZE;
		
		return val;
}



//     下面是构造containerproc部分


static void show_val_kb(struct seq_file *m, const char *s, unsigned long num)
{
	orig_num_to_str = (void *)kallsyms_lookup_name("num_to_str");
	char v[32];
	static const char blanks[7] = {' ', ' ', ' ', ' ',' ', ' ', ' '};
	int len;
   
	len = orig_num_to_str(v, sizeof(v), num << (PAGE_SHIFT - 10));

	seq_write(m, s, 16);

	if (len > 0) {
		if (len < 8)
			seq_write(m, blanks, 8 - len);

		seq_write(m, v, len);
	}
	seq_write(m, " kB\n", 4);
}

static int container_proc_show(struct seq_file *m,void *v){
	    struct task_struct *p,*container;
	    struct mem_cgroup *memcg = NULL;
	    p = current->parent;
	    container = p->parent;
	    printk("Task name:%s,task ID:%d\n",container->comm,container->pid);

		orig_mem_cgroup_nr_lru_pages = (void *)kallsyms_lookup_name("mem_cgroup_nr_lru_pages");
		
		memcg = mem_cgroup_from_task(p);
		memtotal = get_limit_in_bytes(memcg);
		memfree  = get_limit_in_bytes(memcg) - get_usage_in_bytes(memcg);
		available =get_limit_in_bytes(memcg) - get_usage_in_bytes(memcg) + get_cache(memcg);
		buffers = 0;
		cached = 0;
		swapcached = 0;
		swaptotal = get_memsw_limit_in_bytes(memcg);
		swapfree = swaptotal - (get_memsw_usage_in_bytes(memcg) - get_usage_in_bytes(memcg));
        shemem = 0 | get_total_shmes(memcg);
		
		unsigned long stat[MEMCG_NR_STAT];
		int i;
		tree_stat(memcg,stat);
		
		
		seq_printf(m,"MemTotal:\t\t\t%ld KB\n",memtotal/1024);
		seq_printf(m,"MemFree:\t\t\t%ld KB\n",memfree/1024);
		seq_printf(m,"usage in bytes:\t\t\t%lld KB\n",get_usage_in_bytes(memcg)/1024);
		seq_printf(m,"Cached:\t\t\t\t%ld KB\n",get_cache(memcg)/1024);
		seq_printf(m,"MemAvailable:\t\t\t%ld KB\n",available/1024);
		seq_printf(m,"Buffers:\t\t\t%ld KB\n",buffers);
		seq_printf(m,"SwapCached:\t\t\t%ld KB\n",swapcached);
		
		for (i = 0; i < NR_LRU_LISTS; i++) {
			struct mem_cgroup *mi;
			unsigned long val = 0;

			for_each_mem_cgroup_tree(mi, memcg)
				val += orig_mem_cgroup_nr_lru_pages(mi, BIT(i));
			seq_printf(m, "%s\t\t\t%llu\n",mem_cgroup_lru_names[i], (u64)val * PAGE_SIZE);
	}
		seq_printf(m,"SwapTotal\t\t\t%ld KB\n",swaptotal/1024);
		seq_printf(m,"SwapFree\t\t\t%ld KB\n",swapfree/1024);
		seq_printf(m,"Shmem\t\t\t%ld KB\n",shemem/1024);

		return 0;
}

static int container_proc_open(struct inode *inode,struct file *file){
		return single_open(file,container_proc_show,NULL);
}

static struct file_operations container_proc_fops = {
		.owner   = THIS_MODULE,
		.open    = container_proc_open,
		.release = single_release,
		.read    = seq_read,
		.llseek  = seq_lseek,
};

static int create_container_proc(void){
		struct proc_dir_entry *file;
		file = proc_create("containermeminfo",0644,NULL,&container_proc_fops);
		if(!file)
			return -ENOMEM;
		return 0;
}


