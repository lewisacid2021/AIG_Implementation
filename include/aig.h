#pragma once
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cassert>
#include <iostream>
#include <stdexcept>

// -------------------------
// 节点表示
// -------------------------
struct AigNode {
    uint32_t fanin0 = 0;
    uint32_t fanin1 = 0;
    bool is_input = false;
};

// -------------------------
// 字面量操作
// -------------------------
static inline uint32_t make_lit(uint32_t id, bool inv=false) {
    return (id << 1) | static_cast<uint32_t>(inv);
}

static inline uint32_t lit_id(uint32_t lit) {
    return lit >> 1;
}

static inline bool lit_inv(uint32_t lit) {
    return lit & 1;
}

// -------------------------
// AIG 图
// -------------------------
class AigGraph {
public:
    std::vector<AigNode> nodes;
    std::vector<uint32_t> inputs;
    std::vector<uint32_t> outputs;

public:
    // 构造函数
    AigGraph();

    // 节点创建
    uint32_t addInput();
    uint32_t addAnd(uint32_t lit0, uint32_t lit1); // 如果输入非法，会抛异常
    void addOutput(uint32_t lit);                  // 如果 lit 对应节点不存在，会抛异常

    // 深度计算
    uint32_t depth() const;

    // 全局优化（去重 + 常量传播）
    void optimize();

    // 统计信息
    void print_stats() const;  // 输出格式: pis=2, pos=2, area=4, depth=2, not=4

private:
    uint32_t depthRec(uint32_t id, std::vector<int>& memo) const;
    uint32_t countAnds() const;
    uint32_t countInverters() const;
};
    
// -------------------------
// AIGER 文件读取
// -------------------------
bool read_aiger_file(const std::string& filename, AigGraph& aig);
