# A library for Network Decision Diagram in **Parallel**

* ConcurrentHashMap
* parallelStream

## 问题分析

1. JDD 不支持多线程访问 
2. Java 的 parallelStream() 默认使用一个共享的线程池，所有 parallelStream() 都共用这个线程池， 外层 parallelStream() 会占用 ForkJoinPool 中的大部分线程，内层流也要从 ForkJoinPool 中获取线程，但线程都被外层占着，池中没线程了，然后死锁了
3. 上层并行，下层串行的方式是行不通的。下层必须支持并行操作

## License

Apache-2.0 License, see [LICENSE](LICENSE).
