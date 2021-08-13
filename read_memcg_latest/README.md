## 简介

本内核模块可以统计docker容器所有进程的内存使用信息，最终目标是得出容器进程使用的物理页属于哪一个memory cgroup。
从而判断容器进程的物理页是否正确被对应的memory cgroup所统计（即进程所在的memory cgroup是否和进程占用物理页中指向的memory cgroup是否一样）。
最后获得的信息通过/proc/MemcgInfo展示。
* 注意：insmod加载模块时需要输入一个containerd-shim进程的pid作为参数。

## 实现思路
* <mark>收集容器中的所有进程<mark>： 因为我们已经得到其中一个shim进程的pid，那么容器中所有进程都是shim进程的子进程，只需要递归遍历shim进程
的所有子进程即可。(还有一种思路就是，通过是否处于同一pid_namespace来判断是否在同一容器中)

* <mark>找到容器进程所属的memory cgroup并纪录其cgroup path (cgroup path类似于`lssubsys -m`输出的信息)<mark>: 这里的实现参考了内核中memcg和cgroup namespace的源码[proc_cgroup_show](https://elixir.bootlin.com/linux/v5.10/source/kernel/cgroup/cgroup.c#L5813)，

