# MTPNDD feature/cidx Development Guide

This directory contains comprehensive documentation for developing parallelization features in the feature/cidx branch.

---

## 📚 Documentation Map

### For Getting Started
1. **[QUICK_START_CIDX.md](QUICK_START_CIDX.md)** ⭐ **START HERE**
   - 10-minute overview of what we're doing
   - Standard 4-5 hour feature development cycle
   - Common patterns and quick troubleshooting
   - Best for: Quick reference, first-time contributors

2. **[CLAUDE.md](../CLAUDE.md)**
   - High-level project overview
   - Branch strategy and goals
   - Parallelization strategy overview
   - Best for: Understanding the bigger picture

### For Detailed Workflow
3. **[WORKFLOW_GUIDE.md](WORKFLOW_GUIDE.md)** ⭐ **COMPREHENSIVE REFERENCE**
   - Complete step-by-step development process
   - 7 phases: Baseline → Design → Implementation → Testing → Documentation → Commit → Integration
   - Troubleshooting section for common issues
   - Checklist for feature completion
   - Best for: In-depth implementation guidance, reference during development

### For Creating Features
4. **[features/TEMPLATE.md](features/TEMPLATE.md)**
   - Feature document template
   - All sections needed to document a parallelization feature
   - Copy and fill in for each new feature
   - Best for: Creating new feature documents

### Existing Feature Documents
5. **[features/baseline-feature-index.md](features/)** (create on first use)
   - Baseline metrics from feature/index branch
   - Measurement methodology
   - Reference for all future features

6. **[features/parallel-[operation].md](features/)** (multiple as you implement)
   - One document per parallelized operation
   - Problem → Analysis → Implementation → Results → Lessons

---

## 🎯 Quick Navigation by Task

### "I'm just getting started"
→ Read **QUICK_START_CIDX.md** (10 min)
→ Understand branch strategy in **CLAUDE.md** (10 min)

### "I'm about to implement a feature"
→ Follow phase-by-phase in **WORKFLOW_GUIDE.md**
→ Create new feature document from **features/TEMPLATE.md**

### "I need to parallelize mtpndd_and"
→ Follow **QUICK_START_CIDX.md** example section
→ Reference Lace patterns in **CLAUDE.md**

### "Something went wrong during testing"
→ Check "Red Flags" section in **CLAUDE.md**
→ Check "Troubleshooting" section in **WORKFLOW_GUIDE.md**
→ Check specific issue in **QUICK_START_CIDX.md**

### "I want to understand the full process"
→ Start with **QUICK_START_CIDX.md** for overview
→ Then **WORKFLOW_GUIDE.md** for comprehensive walkthrough
→ Reference **features/TEMPLATE.md** for documentation structure

---

## 📋 Development Workflow at a Glance

```
1. ESTABLISH BASELINE (from feature/index)
   └─ Measure memory and time with nqueens N=12
   └─ Document in docs/features/baseline-*.md

2. DESIGN PARALLELIZATION
   └─ Analyze current operation
   └─ Plan parallel strategy
   └─ Create feature design document

3. IMPLEMENT CHANGES
   └─ Create feature branch: feat/parallel-[operation]
   └─ Implement using Lace task framework
   └─ Commit logically, test frequently

4. TEST PERFORMANCE
   └─ Run baseline and optimized versions
   └─ Compare metrics (memory, time, speedup)
   └─ Validate correctness on 1/2/4 workers

5. DOCUMENT RESULTS
   └─ Update feature document with measurements
   └─ Record unexpected findings
   └─ List lessons learned

6. COMMIT & MERGE
   └─ Final commit with clear message
   └─ Link to feature documentation
   └─ Merge to feature/cidx

7. PREPARE FOR NEXT FEATURE
   └─ Update baseline for next operation
   └─ Plan next parallelization
```

**Total per feature: ~4-5 hours**

---

## 🔧 Tools & Commands Reference

### Building
```bash
# Development (with instrumentation)
cmake -B build -DMTPNDD_ENABLE_RECORDING=ON
cmake --build build

# Production (clean)
cmake -B build && cmake --build build --clean-first
```

### Testing
```bash
# Correctness
./build/src/sylvan/mtpndd/test/mtpndd_nqueens_test 8

# Performance
./build/src/sylvan/mtpndd/test/mtpndd_nqueens_exp 12 1  # Baseline
./build/src/sylvan/mtpndd/test/mtpndd_nqueens_exp 12 4  # Parallel
```

### Measuring
```bash
# Memory profiling
./tools/measure_memory.sh proc ./build/.../mtpndd_nqueens_exp 12 1

# Detailed Valgrind analysis
./tools/measure_memory.sh massif ./build/.../mtpndd_nqueens_exp 12 1
```

### Git
```bash
# Create feature branch
git checkout -b feat/parallel-[operation-name]

# Commit with clear message
git commit -m "feat(cidx): parallelize [operation]

- Description of changes
- Results: [brief summary]
- See: docs/features/parallel-[operation].md"

# Merge back
git checkout feature/cidx
git merge --no-ff feat/parallel-[operation-name]
```

---

## 📊 Success Criteria

Each parallelization feature should achieve:

✅ **Correctness**
- Compiles without warnings
- Results match baseline (all worker counts)
- No memory leaks (Valgrind clean)

✅ **Performance**
- Speedup > 5% on 4+ workers
- Single-worker regression < 5%
- Memory stable (< 5% change)

✅ **Documentation**
- Feature document complete
- Code changes clear and traceable
- Lessons and insights recorded

✅ **Quality**
- Clean git history
- Clear commit messages
- Tests reproducible

---

## 🎓 Learning Resources in Repo

### Core Architecture
- **docs/memory_optimization_analysis.md** - Memory breakdown and optimization paths
- **docs/dot/mtpndd_architecture.png** - Module architecture diagram
- **docs/dot/mtpndd_memory_design_zh.png** - Memory layout diagram

### Parallelization
- **CLAUDE.md** - Lace framework overview and patterns
- **WORKFLOW_GUIDE.md** - Detailed sync patterns and integration points
- **sylvan/src/lace/lace.h** - Lace API documentation

### Previous Optimizations
- **docs/temp_refs_results.md** - Previous optimization example
- **docs/optimization_analysis.md** - Optimization strategy overview

---

## 🚀 Phases at a Glance

| Phase | Duration | Key Activity | Output |
|-------|----------|--------------|--------|
| Baseline | 15 min | Measure feature/index | Baseline metrics |
| Design | 30 min | Plan parallelization | Design document |
| Implement | 1-2 hrs | Code parallel version | Working code |
| Test | 30 min | Measure performance | Metrics & analysis |
| Document | 30 min | Record findings | Feature document |
| Commit | 10 min | Push to git | Feature merged |
| **Total** | **~4-5 hrs** | **Complete feature** | **Production feature** |

---

## ⚡ First Feature Checklist

Before starting your first parallelization:

- [ ] Understand branch strategy (CLAUDE.md)
- [ ] Read quick start (QUICK_START_CIDX.md)
- [ ] Understand standard cycle (4-5 hours)
- [ ] Have access to build environment
- [ ] Read Lace patterns (CLAUDE.md + Lace docs)
- [ ] Identify first operation to parallelize (AND recommended)
- [ ] Create feature design document (from TEMPLATE.md)

---

## 📞 When Stuck

### Correctness Issues
→ Check WORKFLOW_GUIDE.md "Troubleshooting" section
→ Use Valgrind: `valgrind --leak-check=full ./binary`
→ Test with -w1 first

### Performance Issues
→ Check "Red Flags" section in CLAUDE.md
→ Measure granularity impact
→ Profile with instrumentation enabled

### Understanding Code
→ Check mtpndd_node.c for operation examples
→ Read Lace headers (sylvan/src/lace/)
→ Review previous feature documents for patterns

---

## 📖 Reading Order for Different Roles

### New Team Member
1. QUICK_START_CIDX.md (overview)
2. CLAUDE.md (project context)
3. WORKFLOW_GUIDE.md (detailed process)
4. A completed feature document (example)

### Experienced Developer
1. QUICK_START_CIDX.md (patterns reference)
2. WORKFLOW_GUIDE.md (as needed reference)
3. TEMPLATE.md (create new features)

### Code Reviewer
1. CLAUDE.md (architecture overview)
2. Feature document (what changed, why)
3. Related commits (implementation)

### Research/Analysis Role
1. All optimization documents
2. WORKFLOW_GUIDE.md (methodology)
3. All feature documents (trends)

---

## 🔗 Related Documentation

- **[../CLAUDE.md](../CLAUDE.md)** - Project overview, architecture, branch strategy
- **[../README.md](../README.md)** - Initial project setup and build instructions
- **[./memory_optimization_analysis.md](./memory_optimization_analysis.md)** - Detailed memory breakdown
- **[./MEMORY_OPTIMIZATION_GUIDE.md](./MEMORY_OPTIMIZATION_GUIDE.md)** - Memory optimization tips

---

## 📝 Document Update History

| Date | Document | Update |
|------|----------|--------|
| 2025-01-31 | QUICK_START_CIDX.md | Created |
| 2025-01-31 | WORKFLOW_GUIDE.md | Created |
| 2025-01-31 | DEVELOPMENT.md | Created |
| 2025-01-31 | features/TEMPLATE.md | Created |

---

**Last Updated**: 2025-01-31

For questions about the workflow or documentation, refer to the appropriate section above or check the Troubleshooting section in WORKFLOW_GUIDE.md.
