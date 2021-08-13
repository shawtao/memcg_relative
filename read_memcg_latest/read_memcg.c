#include "read_memcg.h"
#include "containerproc.h"


/* pass the docker process's pid as param over insmod */
module_param(docker_pid, int, 0644);

/*
    通过四级页表将虚拟地址转换为其映射的页框(struct page)，并取出page->mem_cgroup
    与docker process的mem_cgroup比较。

    @mm : 进程的mm_struct
    @virt : 虚拟地址

    return 1 : page->mem_cgroup == docker mem_cgroup
    return 0 : page->mem_cgroup == NULL
    return -1 : 没有找到映射的struct page，可能这部分虚拟地址还没进行映射

*/
static int Vma2pages(struct mm_struct *mm, unsigned long virt, struct vm_area_struct *vma, struct docker_tasks *dtsk)
{
    pgd_t *pgd;
    pte_t *ptep, pte;
    pud_t *pud;
    pmd_t *pmd;
    p4d_t *p4d;
    struct page *page;
    unsigned long page_addr;

    pgd = pgd_offset(mm, virt);
    if (pgd_none(*pgd) || pgd_bad(*pgd)) {
        pr_info("UnValid pgd\n");
        goto out;
    }

    p4d = p4d_offset(pgd, virt);
    if (p4d_none(*p4d) || p4d_bad(*p4d)) {
        pr_info("UnValid p4d\n");
        goto out;
    }
    
    pud = pud_offset(p4d, virt);
    if (pud_none(*pud) || pud_bad(*pud)) {
        pr_info("UnValid pud\n");
        goto out;
    }

    pmd = pmd_offset(pud, virt);
    if (pmd_none(*pmd) || pmd_bad(*pmd)) {
        pr_info("UnValid pmd\n");
        goto out;
    }

    ptep = pte_offset_map(pmd, virt);
    if(!ptep) {
        pr_info("UnValid pte\n");
        goto out;
    }

    pte = *ptep;
    page = pte_page(pte);
    page_addr = (unsigned long)page;
    //pr_info("pageaddr: %lu\n",page_addr);

    if(page && (unsigned long)page != NOT_MAP_PAGE_ADDR && page_count(page)) {  /*经过观察发现，没有进行映射的虚拟地址都会映射到NOT_MAP_PAGE_ADDR地址的struct page上 */
        //pr_info("page frame struct is @ %p\n",page);
        struct page_stats *pgs;
        struct mem_cgroup *page_mem_cg = page->mem_cgroup;

        pgs = (struct page_stats*)kmalloc(sizeof(struct page_stats), GFP_KERNEL);
        if(!pgs) {
            pr_info("Alloc page_stats failed!\n");
            goto out;
        }
        pgs->page_memcg = page_mem_cg;
        pgs->vma = vma;
        pgs->page = page;

        if(page_mem_cg == dtsk->task_memcg) {
            if(!HasDupPage(page, dtsk)) {
                list_add_tail(&(pgs->list), &(dtsk->memcg_equal));
            } else {
                pr_info("Dup Page");
                kfree(pgs);
            }
            printk("Equal! page frame struct is @ %p ",page);
            //PrintPageType(page);
            return 1;            
        }
        if(!HasDupPage(page, dtsk)) {
            list_add_tail(&(pgs->list), &(dtsk->memcg_notequal));
        } else {
            pr_info("Dup Page");
            kfree(pgs);
        }
        printk("Not queal! page frame struct is @ %p ",page);
        //PrintPageType(page);
        return 0;
    }
out: 
    return -1;
}

/*
    遍历一个process的所有VMA,并且在以每个VMA的起始虚拟地址以PAGE_SIZE 4KB为单位转换为对应的物理页框。

    @tsk : process's task_struct;
*/
static int TravseVMA(struct docker_tasks *dtsk)
{   
    struct task_struct *tsk = dtsk->tsk;
    struct mm_struct *mm;
    struct vm_area_struct *vma;
    unsigned long start, end, length, vpage;
    unsigned int task_rss = 0;
    unsigned int not_count_rss = 0;
    int ret = 0;
    int j = 0;

    mm = tsk->mm;
    vma = mm->mmap;

	down_read(&mm->mmap_sem);
	pr_info("vmas:                vma        start          end        length\n");

	while (vma) {
        unsigned int vma_rss_count = 0;
        unsigned int vma_not_count = 0;

		j++;
		start = vma->vm_start;
		end = vma->vm_end;
		length = end - start;
		pr_info("\n%6d: %16p %12lx %12lx   %8ld\n",
			j, vma, start, end, length);
       
        for(vpage = vma->vm_start; vpage < vma->vm_end; vpage += PAGE_SIZE) {
            ret = Vma2pages(mm, vpage, vma, dtsk);
            if(ret < 0){
                pr_info("Vma2pages Failed! Maybe didn't map.\n");
                continue;
            }
            if(ret == 0)
                vma_not_count++;
            vma_rss_count++;
        }
        pr_info("Total RSS = %uKB, Not count RSS = %uKB\n", vma_rss_count * 4, vma_not_count * 4);
        task_rss += vma_rss_count;
        not_count_rss += vma_not_count;
		vma = vma->vm_next;
	}

	up_read(&mm->mmap_sem);

    dtsk->task_rss = task_rss * 4;
    dtsk->not_count_rss = not_count_rss * 4;

    return 0;   
}

/*
    获得进程所属的struct mem_cgroup并返回
    @tsk : process's task_struct

*/
static struct mem_cgroup *GetMemcgroup(struct task_struct *tsk)
{
	struct css_set *cssset = tsk->cgroups;
	//struct cg_group_link cglink = NULL;
	struct cgroup_subsys_state *cgsysstate;
	struct cgroup_subsys *cgsys;
    struct mem_cgroup *mcg = NULL;
    int i;

    for(i = 0 ; i <= CGROUP_SUBSYS_COUNT; i++){
        cgsysstate = cssset->subsys[i];
        cgsys = cgsysstate->ss;
        if(cgsys->id == 4){         /*cgroup memory子系统的id为4 */
            //pr_info("subsys:%s!",cgsys->name);
            mcg = container_of(cgsysstate, struct mem_cgroup, css);
            if(mcg){
               // pr_info("Successfully get mem_cgroup!");
                return mcg;
            }

        }
    }
		
	return mcg;
}

static int MemcgPath(struct mem_cgroup *memcg, char *buf, size_t buflen)
{
    struct cgroup_subsys_state *cgsysstate;
    struct cgroup *cgrp;
    struct css_set *orig_init_css_set;
    struct cgroup *(*orig_cset_cgroup_from_root)(struct css_set *cset, struct cgroup_root *root);

    cgsysstate = &(memcg->css);
    cgrp = cgsysstate->cgroup;
    orig_cset_cgroup_from_root = (void *)kallsyms_lookup_name("cset_cgroup_from_root");
    orig_init_css_set = (void *)kallsyms_lookup_name("init_css_set");

    struct cgroup *root = orig_cset_cgroup_from_root(orig_init_css_set, cgrp->root);
    //struct cgroup *root = &(cgrp->root->cgrp);
    if(root == NULL || cgrp == NULL){
        pr_info("Cset_cgroup_from_root failed!");
        return -1;
    }

    return kernfs_path_from_node(cgrp->kn, root->kn, buf, buflen);
}

static void DfsProcess(struct task_struct *tsk, struct docker_tasks *docker_tasks_head)
{
    struct task_struct *child;
    struct list_head *list;
    struct docker_tasks *dtsk;

    if(strcmp(tsk->comm,"containerd-shim") == 0)
        goto dfs;
    
    dtsk = (struct docker_tasks*)kmalloc(sizeof(struct docker_tasks), GFP_KERNEL);
    if(!dtsk){
        pr_info("Alloc dtsk failed!\n");
    }
    dtsk->tsk = tsk;
    list_add_tail(&(dtsk->list), &(docker_tasks_head->list));

dfs:   
    list_for_each(list, &tsk->children) {
        child = list_entry(list, struct task_struct, sibling);
        DfsProcess(child,docker_tasks_head);
    }
}

static struct docker_tasks *CollectDockerProcess(struct task_struct *tsk)
{
    struct docker_tasks *docker_tasks_head;
    docker_tasks_head = (struct docker_tasks*)kmalloc(sizeof(struct docker_tasks), GFP_KERNEL);
    INIT_LIST_HEAD(&(docker_tasks_head->list));
    DfsProcess(tsk,docker_tasks_head);

    return docker_tasks_head;
}

static void PrintPageType(struct page *p, char *pagetype)
{   
    if (PageLocked(p))
	    strcat(pagetype, "Locked ");
	if (PageReserved(p))
		strcat(pagetype, "Reserved ");
	if (PageSwapCache(p))
		strcat(pagetype, "SwapCache ");
	if (PageReferenced(p))
		strcat(pagetype, "Referenced ");
	if (PageSlab(p))
		strcat(pagetype, "Slab ");
	if (PagePrivate(p))
		strcat(pagetype, "Private ");
	if (PageUptodate(p))
		strcat(pagetype, "Uptodate ");
	if (PageDirty(p))
		strcat(pagetype, "Dirty ");
	if (PageActive(p))
		strcat(pagetype, "Active ");
	if (PageWriteback(p))
		strcat(pagetype, "WriteBack ");
	if (PageMappedToDisk(p))
		strcat(pagetype, "MappedToDisk ");
    if(p->mapping != NULL) {
        if(PageAnon(p)) {
            strcat(pagetype, "Page_Anon ");
        }else {
            strcat(pagetype, "Page_File ");
        }
    }

}

static int HasDupPage(struct page *page,struct docker_tasks *dtks)
{
    struct list_head *equal_pos;
    struct list_head *notequal_pos;
    struct page_stats *equal_pgs;
    struct page_stats *notequal_pgs;
    list_for_each(equal_pos, &(dtks->memcg_equal)) {
        equal_pgs = list_entry(equal_pos, struct page_stats, list);
        if(equal_pgs->page == page)
            return 1;
    }
    list_for_each(notequal_pos, &(dtks->memcg_notequal)) {
        notequal_pgs = list_entry(notequal_pos, struct page_stats, list);
        if(notequal_pgs->page == page)
            return 1;
    }

    return 0;
}

static void CountTaskRss(struct docker_tasks *dtks_head)
{
    struct list_head *pos;
    struct docker_tasks *dtks;
    list_for_each(pos, &(dtks_head->list)) {
        dtks = list_entry(pos, struct docker_tasks, list);
        struct list_head *equal_pos;
        struct list_head *notequal_pos;
        struct page_stats *equal_pgs;
        struct page_stats *notequal_pgs;
        int equal_page_count = 0;
        int notequal_page_count = 0;
        list_for_each(equal_pos, &(dtks->memcg_equal)) {
            equal_page_count ++;
        }
        list_for_each(notequal_pos, &(dtks->memcg_notequal)) {
            notequal_page_count ++;
        }
        dtks->not_count_rss = notequal_page_count * 4;
        dtks->task_rss = (equal_page_count + notequal_page_count) * 4;       
    }
}

static int ReadMemcg(struct docker_tasks *docker_tasks_head)
{
    struct list_head *pos;
    struct docker_tasks *dtsk;
    int ret = -1;

    list_for_each(pos, &(docker_tasks_head->list)){
        dtsk = list_entry(pos, struct docker_tasks, list);
        INIT_LIST_HEAD(&dtsk->memcg_equal);
        INIT_LIST_HEAD(&dtsk->memcg_notequal);
        dtsk->task_memcg = GetMemcgroup(dtsk->tsk);
       // pr_info("name: %s, pid: [%d], state: %li\n", dtsk->tsk->comm, dtsk->tsk->pid, dtsk->tsk->state);

        dtsk->memcg_path = kmalloc(PATH_MAX, GFP_KERNEL);
        if(!dtsk->memcg_path){
            pr_info("Buf alloc failed!\n");
            goto buf_error;
        }

        ret = MemcgPath(dtsk->task_memcg, dtsk->memcg_path, PATH_MAX);
        if(ret < 0){
            pr_info("Get MemcgPath failed!  errno = %d\n", ret);
            goto path_error;
        }

        pr_info(" Examining vma's for pid=%d, command=%s, memcg_path = %s\n",dtsk->tsk->pid, dtsk->tsk->comm, dtsk->memcg_path);

        ret = TravseVMA(dtsk);   
    }
    return ret;

path_error:
    kfree(dtsk->memcg_path);
buf_error:
    return ret;
}


static int __init read_memcg_init(void)
{
    struct task_struct *docker_tsk;
    struct pid_namespace *pid_ns;
    struct docker_tasks *dtsks;
    int ret = 0;

    docker_tsk = pid_task(find_vpid(docker_pid), PIDTYPE_PID);
    if(!docker_tsk){   
        pr_info("Can't find docker's task_struct! docker_pid = %d\n",docker_pid);
        return -1;
    }
    pid_ns = docker_tsk->nsproxy->pid_ns_for_children;
    pid_ns_init_process = pid_ns->child_reaper;
    //pr_info("pid_namespace init process's pid = %d\n",pid_ns->child_reaper->pid);
    dtsks = CollectDockerProcess(pid_ns_init_process->parent);

    ret = ReadMemcg(dtsks);
    if(ret < 0){
        pr_info("ReadMemcg failed!\n");
        return ret;
    }

    CountTaskRss(dtsks);
    all_docker_tasks = dtsks;

    ret = create_memcg_proc();
    if(ret < 0){
        pr_info("Create memcg porc failed!\n");
        return ret;
    }

    return ret;
}

static void __exit read_memcg_exit(void)
{   
    remove_proc_entry("MemcgInfo",NULL);
	pr_info("Module exit\n");
}

module_init(read_memcg_init);
module_exit(read_memcg_exit);