## 简介

本内核模块可以统计docker容器所有进程的内存使用信息，最终目标是得出容器进程使用的物理页属于哪一个memory cgroup。
从而判断容器进程的物理页是否正确被对应的memory cgroup所统计（即进程所在的memory cgroup是否和进程占用物理页中指向的memory cgroup是否一样）。
最后获得的信息通过/proc/MemcgInfo展示。
* 注意：insmod加载模块时需要输入一个containerd-shim进程的pid作为参数。

## 实现思路
* **收集容器中的所有进程**： 因为我们已经得到其中一个shim进程的pid，那么容器中所有进程都是shim进程的子进程，只需要递归遍历shim进程的所有子进程即可。(还有一种思路就是，通过是否处于同一pid_namespace来判断是否在同一容器中)

* **找到容器进程所属的memory cgroup并纪录其cgroup path (cgroup path类似于`lssubsys -m`输出的信息)**: 这里的实现参考了内核中memcg和cgroup namespace的源码[proc_cgroup_show](https://elixir.bootlin.com/linux/v5.10/source/kernel/cgroup/cgroup.c#L5813)。从task_struct到mem cgroup需要经过`css_set`和`cgroup_subsys_state`，然后再通过`container_of`获得`mem_cgroup`.不过后来发现内核有相应的函数可以直接从task_struct获得mem_cgroup，某些static inline函数需要通过`kallsyms_lookup_name`找到符号再调用。

* **统计进程RSS中有多少页面被memcg统计，有多少没被统计**：因为当一个page被memcg charge的话，会对`struct page`的`mem_cgroup`字段赋值，所以核心是要找到`struct page`中的`mem_cgroup`字段，来看当前page是否被正确的memcg charge. 所以首先需要遍历容器进程的virtual memory，同过`struct mm_struct`中的mmap字段我们可以获得vma(`struct vm_area_struct`)的链表，从而遍历所有vma.
对于每一个vma，以`PAGES_SIZE`为单位从vm_start遍历到vm_end；对于每一个virtual address，通过四级页表翻译对应的api (`pgd_offset`,`p4d_offset`,`pud_offset`,`pmd_offset`,`pte_page`等) ，最后得到virtual address对应的physical address对应的struct page，最后即可取出page对应的memcg。**注意**：遍历vma时需要加锁.

* **统计进程VM和RSS信息**：在遍历vma和其映射的page的总数其实我们就可以计算出进程使用的RSS了，这个方法据我所知`/proc/pid/maps`和`/proc/pid/smaps`就是这样统计进程内存信息的，算出来的数据也和这两个目录算出来的数据差不多，但是有一些细微偏差，感觉是对某些页的处理有所偏差导致。这里还要注意的是：**所有没建立映射的VM通过4级页表翻译都会映射到一个相同的地址**，可能需要再研究下缺页中断相关的代码。

* **在/proc下创建MemcgInfo文件并输出信息**：特意维护`stuct docker_tasks`和`struct page_stats`两个结构体，并且都通过链表链起来纪录进程对应的信息，主要是为了熟悉Linux内核双向链表API的使用。因为之前写的dockermeminfo模块已经用过/proc下创建文件的api，所以这里就不多说了。至于怎么输出信息，通过借鉴`/proc/meminfo`实现的源码即可。
