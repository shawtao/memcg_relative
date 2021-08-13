#ifndef READ_MEMCG_H
#define READ_MEMCG_H

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/unistd.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/memcontrol.h>
#include <linux/cgroup.h>
#include <linux/cgroup-defs.h>
#include <linux/kallsyms.h> 
#include <linux/pid_namespace.h>
#include <linux/slab.h>
#include <linux/string.h>

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("shawtao"); 
MODULE_DESCRIPTION("read mem_cgroup");

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define NOT_MAP_PAGE_ADDR 18446695582455037952

static int docker_pid;
struct docker_tasks *all_docker_tasks;
struct task_struct *pid_ns_init_process;

struct page_stats {
    struct vm_area_struct *vma;
    struct mem_cgroup *page_memcg;
    struct page *page;
    struct list_head list;
};

struct docker_tasks { 
    struct task_struct *tsk;
    struct list_head list;
    struct mem_cgroup *task_memcg;
    unsigned int task_rss;
    unsigned int not_count_rss;
    char *memcg_path;
    struct list_head memcg_equal;
    struct list_head memcg_notequal;
};

static int TravseVMA(struct docker_tasks *dtsk);
static int Vma2pages(struct mm_struct *mm, unsigned long virt, struct vm_area_struct *vma, struct docker_tasks *dtsk);
static struct mem_cgroup *GetMemcgroup(struct task_struct *tsk);
static int MemcgPath(struct mem_cgroup *memcg, char *buf, size_t buflen);
static struct docker_tasks *CollectDockerProcess(struct task_struct *tsk); 
static void DfsProcess(struct task_struct *tsk, struct docker_tasks *docker_tasks_head);
static void PrintPageType(struct page *p, char *pagetype);
static int HasDupPage(struct page *page,struct docker_tasks *dtks);
static void CountTaskRss(struct docker_tasks *dtks);

static int ReadMemcg(struct docker_tasks *docker_tasks_head);

#endif

