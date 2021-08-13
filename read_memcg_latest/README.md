## Introduction

本内核模块可以统计docker容器所有进程的内存使用信息，最终目标是得出容器进程使用的物理页属于哪一个memory cgroup。
从而判断容器进程的物理页是否正确被对应的memory cgroup所统计。最后获得的信息通过/proc/MemcgInfo展示。
