# A library for Network Decision Diagram in **Parallel**

使用 C 语言重构 NDD

## Build and Run

* Sphinx for doc generating

https://www.sphinx-doc.org/en/master/usage/installation.html

for macOS,

```bash
brew install sphinx-doc
```

* Sylvan with Lace

```bash
cd sylvan
mkdir build
cd build
cmake ..
make && make test
```

## 问题分析

1. 在上层也使用 Lace 框架，使用全局统一的 Lace 还是上下层分离的互不干扰？

## License

Apache-2.0 License, see [LICENSE](LICENSE).
