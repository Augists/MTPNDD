# A library for Network Decision Diagram in **Parallel**

* ConcurrentHashMap
* parallelStream

## 问题分析

1. JDD 不支持多线程访问 
2. Java 的 parallelStream() 默认使用一个共享的线程池，所有 parallelStream() 都共用这个线程池， 外层 parallelStream() 会占用 ForkJoinPool 中的大部分线程，内层流也要从 ForkJoinPool 中获取线程，但线程都被外层占着，池中没线程了，然后死锁了
3. 上层并行，下层串行的方式是行不通的。下层必须支持并行操作

1. 遇到的一个问题是，例如在 NQueens 中我们使用了多个 Solution，会造成对每个 Solution 初始化一次 NDD，从而使得在底层的 Sylvan 重复的初始化，并且共用节点表 -> 可以用单例模式解决

1. 上层并行通过 JNI 调用 Sylvan，可能会在两个层面上不支持并行
    1. JNI 调用本身不一定是线程安全的，涉及到 JNIEnv 指针
    2. 底层会共享全局变量或有共享资源，自己本身可以并行但是并不支持线程安全的并行访问

## License

Apache-2.0 License, see [LICENSE](LICENSE).
