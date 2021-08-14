## 简介
针对在docker中/proc/meminfo文件并没有隔离的问题，本内核模块可以截获docker容器进程发起的对/proc/meminfo文件的open操作，并根据memory cgroup的内存信息对容器/proc/meminfo文件的信息进行重新整合，从而使容器进程中打开/proc/meminfo时观察到的是容器的内存信息。达到宿主机和容器/proc/meminfo文件的隔离。
