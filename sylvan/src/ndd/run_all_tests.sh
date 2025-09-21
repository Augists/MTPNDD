#!/bin/bash

# MTPNDD 项目全部测试运行脚本
# 用于验证新的测试文件结构

echo "🚀 运行MTPNDD项目全部测试套件..."
echo ""

# 编译所有测试
echo "📦 编译所有测试程序..."
make clean
make all

if [ $? -ne 0 ]; then
    echo "❌ 编译失败，停止测试"
    exit 1
fi

echo "✅ 编译成功"
echo ""

# 定义测试列表（按重要程度排序）
CORE_TESTS=(
    "test_memory_management"
    "test_parallel_verification"
    "test_error_handling"
    "test_cache_performance"
    "test_advanced_operations"
)

MTPNDD_TESTS=(
    "test_mtpndd_structure"
    "test_mtpndd_operations"
    "test_mtpndd_encoding"
    "test_mtpndd_serialization"
    "test_mtpndd_logic"
    "test_mtpndd_custom_operations"
    "test_mtpndd_constraints"
)

PARALLEL_TESTS=(
    "test_parallel_logic"
)

# 运行核心测试
echo "🔧 运行核心测试套件..."
for test in "${CORE_TESTS[@]}"; do
    if [ -f "$test" ]; then
        echo "  📋 运行 $test..."
        ./$test > /dev/null 2>&1
        if [ $? -eq 0 ]; then
            echo "  ✅ $test 通过"
        else
            echo "  ❌ $test 失败"
        fi
    else
        echo "  ⚠️  $test 不存在"
    fi
done
echo ""

# 运行MTPNDD测试
echo "🔥 运行MTPNDD多终端测试套件..."
for test in "${MTPNDD_TESTS[@]}"; do
    if [ -f "$test" ]; then
        echo "  📋 运行 $test..."
        ./$test > /dev/null 2>&1
        if [ $? -eq 0 ]; then
            echo "  ✅ $test 通过"
        else
            echo "  ❌ $test 失败"
        fi
    else
        echo "  ⚠️  $test 不存在"
    fi
done
echo ""

# 运行并行测试
echo "⚡ 运行并行测试套件..."
for test in "${PARALLEL_TESTS[@]}"; do
    if [ -f "$test" ]; then
        echo "  📋 运行 $test..."
        ./$test > /dev/null 2>&1
        if [ $? -eq 0 ]; then
            echo "  ✅ $test 通过"
        else
            echo "  ❌ $test 失败"
        fi
    else
        echo "  ⚠️  $test 不存在"
    fi
done
echo ""

echo "🎉 测试完成！"
echo ""
echo "💡 使用方法："
echo "  ./run_all_tests.sh          # 运行所有测试"
echo "  make run_all_tests         # 使用Makefile运行所有测试"
echo "  make test_<name>           # 编译并运行特定测试"
echo ""
echo "📁 测试文件位置："
echo "  所有测试源码位于: tests/ 目录"
echo "  编译的测试程序位于: 当前目录"