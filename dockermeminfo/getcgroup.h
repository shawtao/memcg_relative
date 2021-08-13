#include <linux/page_counter.h>
#include <linux/memcontrol.h>
#include <linux/cgroup.h>
#include <linux/cgroup-defs.h>


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

static unsigned long get_inactive_anon(struct mem_cgroup *mcg){
		return mem_cgroup_nr_lru_pages(mcg,BIT(0) * PAGE_SIZE);
}

static unsigned long get_active_anon(struct mem_cgroup *mcg){
		return mem_cgroup_nr_lru_pages(mcg,BIT(1) * PAGE_SIZE);
}

static unsigned long get_inactive_file(struct mem_cgroup *mcg){
		return mem_cgroup_nr_lru_pages(mcg,BIT(2) * PAGE_SIZE);
}

static unsigned long get_active_file(struct mem_cgroup *mcg){
		return mem_cgroup_nr_lru_pages(mcg,BIT(3) * PAGE_SIZE);
}

static unsigned long get_unevictable(struct mem_cgroup *mcg){
		return mem_cgroup_nr_lru_pages(mcg,BIT(4) * PAGE_SIZE);
}

 static unsigned long gt_total_shmes(struct mem_cgroup *memcg){
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


