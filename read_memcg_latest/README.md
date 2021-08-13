## 简介

本内核模块可以统计docker容器所有进程的内存使用信息，最终目标是得出容器进程使用的物理页属于哪一个memory cgroup。
从而判断容器进程的物理页是否正确被对应的memory cgroup所统计（即进程所在的memory cgroup是否和进程占用物理页中指向的memory cgroup是否一样）。
最后获得的信息通过/proc/MemcgInfo展示。
* 注意：insmod加载模块时需要输入一个containerd-shim进程的pid作为参数。

## 实现思路
* **收集容器中的所有进程**： 因为我们已经得到其中一个shim进程的pid，那么容器中所有进程都是shim进程的子进程，只需要递归遍历shim进程
的所有子进程即可。(还有一种思路就是，通过是否处于同一pid_namespace来判断是否在同一容器中)

* **找到容器进程所属的memory cgroup并纪录其cgroup path (cgroup path类似于`lssubsys -m`输出的信息)**: 这里的实现参考了内核中memcg和cgroup namespace的源码[proc_cgroup_show](https://elixir.bootlin.com/linux/v5.10/source/kernel/cgroup/cgroup.c#L5813)。从task_struct到mem cgroup需要经过`css_set`和`cgroup_subsys_state`，然后再通过`container_of`获得`mem_cgroup`.不过后来发现内核有相应的函数可以直接从task_struct获得mem_cgroup，某些static inline函数需要通过`kallsyms_lookup_name`找到符号再调用。

* **统计进程RSS中有多少页面被memcg统计，有多少没被统计**：因为当一个page被memcg charge的话，会对`struct page`的`mem_cgroup`字段赋值，所以核心是要找到`struct page`中的`mem_cgroup`字段，来看当前page是否被正确的memcg charge. 所以首先需要遍历容器进程的virtual memory，同过`struct mm_struct`中的mmap字段我们可以获得vma(`struct vm_area_struct`)的链表，从而遍历所有vma.
对于每一个vma，以`PAGES_SIZE`为单位从vm_start遍历到vm_end；对于每一个virtual address，通过四级页表翻译对应的api (`pgd_offset`,`p4d_offset`,`pud_offset`,`pmd_offset`,`pte_page`等) ，最后得到virtual address对应的physical address对应的struct page，最后即可取出page对应的memcg。**注意**：遍历vma时需要加锁.
