#include "aig.h"
#include <unordered_map>
#include <stdexcept>
#include <cstdint>
#include <algorithm>
#include <cassert>

// =============================================================
// 构造函数
// =============================================================
AigGraph::AigGraph() {
    // node 0 = constant 0
    // 确保节点0始终存在
    nodes.push_back(AigNode{0,0,false});
}

// =============================================================
// 输入节点
// =============================================================
uint32_t AigGraph::addInput() {
    uint32_t id = nodes.size();
    AigNode n;
    n.is_input = true;
    nodes.push_back(n);
    inputs.push_back(id);
    return id; // 返回 ID，用户需自行转 literal
}

// =============================================================
// AND节点
// =============================================================
uint32_t AigGraph::addAnd(uint32_t lit0, uint32_t lit1) {
    // 简单的 on-the-fly 优化 (可选，但推荐)
    if (lit0 == 0 || lit1 == 0) return 0;          // AND(x, 0) = 0
    if (lit0 == 1) return lit1;                    // AND(1, x) = x
    if (lit1 == 1) return lit0;                    // AND(x, 1) = x
    if (lit0 == lit1) return lit0;                 // AND(x, x) = x
    if (lit0 == (lit1 ^ 1)) return 0;              // AND(x, !x) = 0
    
    // 规范化：让小的 literal 在前，便于哈希去重
    if (lit0 > lit1) std::swap(lit0, lit1);

    uint32_t id0 = lit_id(lit0);
    uint32_t id1 = lit_id(lit1);
    if(id0 >= nodes.size() || id1 >= nodes.size())
        throw std::out_of_range("addAnd: input literal refers to nonexistent node");

    uint32_t id = nodes.size();
    AigNode n;
    n.fanin0 = lit0;
    n.fanin1 = lit1;
    n.is_input = false;
    nodes.push_back(n);
    return make_lit(id, false);
}

// =============================================================
// 输出节点
// =============================================================
void AigGraph::addOutput(uint32_t lit) {
    uint32_t id = lit_id(lit);
    if(id >= nodes.size())
        throw std::out_of_range("addOutput: literal refers to nonexistent node");
    outputs.push_back(lit);
}

// =============================================================
// 深度计算
// =============================================================
uint32_t AigGraph::depth() const {
    // 使用 int 而不是 vector<int> 来避免递归中的重复分配? 
    // 不，这里是一次性计算。
    std::vector<int> memo(nodes.size(), -1);
    uint32_t max_depth = 0;
    for(uint32_t lit: outputs){
        uint32_t d = depthRec(lit_id(lit), memo);
        max_depth = std::max(max_depth, d);
    }
    return max_depth;
}

uint32_t AigGraph::depthRec(uint32_t id, std::vector<int>& memo) const {
    assert(id < nodes.size());
    if(memo[id] >= 0) return memo[id];

    const AigNode& n = nodes[id];
    // 常量0 (id=0) 或 输入节点，深度为 0
    if(id == 0 || n.is_input) { 
        memo[id] = 0; 
        return 0; 
    }

    uint32_t d0 = depthRec(lit_id(n.fanin0), memo);
    uint32_t d1 = depthRec(lit_id(n.fanin1), memo);
    memo[id] = std::max(d0, d1) + 1;
    return memo[id];
}

// =============================================================
// 全局优化（去重 + 常量传播）
// =============================================================
void AigGraph::optimize() {
    std::vector<AigNode> new_nodes;
    std::unordered_map<uint64_t, uint32_t> strash; // Structural Hashing: key -> New Literal (not ID!)
    
    // old2new 存储的是: Old Node ID -> New Literal
    // 这一点至关重要，因为简化可能导致节点变成一个反相的信号
    std::vector<uint32_t> old2new(nodes.size(), 0);

    // 1. 初始化常量 0
    // new_nodes[0] 必须是常量0
    new_nodes.push_back(nodes[0]); 
    old2new[0] = 0; // Old 0 -> Literal 0

    // 2. 处理所有节点
    // 注意：我们要保留原本的 Inputs 顺序，或者重新建立
    // 简单的做法是遍历旧节点数组
    for(size_t id = 1; id < nodes.size(); ++id) {
        const AigNode& n = nodes[id];

        if (n.is_input) {
            // 创建新的输入节点
            uint32_t new_id = new_nodes.size();
            AigNode new_input_node;
            new_input_node.is_input = true;
            new_nodes.push_back(new_input_node);
            
            // 映射旧ID -> 新Literal (id << 1)
            old2new[id] = make_lit(new_id, false);
        } else {
            // AND 节点，先获取其输入在“新图”中的 Literal
            // 公式: NewLit = Map[OldId] ^ OldInv
            uint32_t l0 = old2new[lit_id(n.fanin0)] ^ lit_inv(n.fanin0);
            uint32_t l1 = old2new[lit_id(n.fanin1)] ^ lit_inv(n.fanin1);

            // --- 常量传播与代数简化 ---
            
            // 1. 遇到 0 (Literal 0)
            if (l0 == 0 || l1 == 0) { 
                old2new[id] = 0; // 结果为 False
                continue; 
            }
            
            // 2. 遇到 1 (Literal 1) -> 结果是另一个
            if (l0 == 1) { old2new[id] = l1; continue; }
            if (l1 == 1) { old2new[id] = l0; continue; }

            // 3. 相同输入 AND(x, x) -> x
            if (l0 == l1) { old2new[id] = l0; continue; }

            // 4. 互补输入 AND(x, !x) -> 0
            if (l0 == (l1 ^ 1)) { old2new[id] = 0; continue; }

            // --- 结构哈希 (Strashing) ---
            
            // 规范化顺序，保证 Hash 唯一
            if (l0 > l1) std::swap(l0, l1);

            uint64_t key = (static_cast<uint64_t>(l0) << 32) | l1;
            auto it = strash.find(key);
            
            if (it != strash.end()) {
                // 发现重复结构，直接复用已有的 Literal
                old2new[id] = it->second;
            } else {
                // 创建新节点
                uint32_t new_id = new_nodes.size();
                AigNode new_node;
                new_node.is_input = false;
                new_node.fanin0 = l0;
                new_node.fanin1 = l1;
                new_nodes.push_back(new_node);

                uint32_t res_lit = make_lit(new_id, false);
                old2new[id] = res_lit;
                strash[key] = res_lit;
            }
        }
    }

    // 3. 重映射 Inputs 列表 (IDs)
    // 注意：旧的 inputs 列表存储的是 IDs。
    // 我们需要更新为新的 IDs。
    std::vector<uint32_t> new_inputs;
    for (uint32_t old_in_id : inputs) {
        // old2new 存的是 Literal，转回 ID
        new_inputs.push_back(lit_id(old2new[old_in_id]));
    }
    inputs = new_inputs;

    // 4. 重映射 Outputs 列表 (Literals)
    for (uint32_t& lit : outputs) {
        // Output 也是通过 Map[OldID] ^ OldInv 计算
        lit = old2new[lit_id(lit)] ^ lit_inv(lit);
    }

    // 5. 替换图
    nodes.swap(new_nodes);
}

// =============================================================
// 统计
// =============================================================
uint32_t AigGraph::countAnds() const {
    uint32_t cnt = 0;
    // 从1开始，跳过常量0
    for(size_t i = 1; i < nodes.size(); ++i) {
        if(!nodes[i].is_input) cnt++;
    }
    return cnt;
}

uint32_t AigGraph::countInverters() const {
    // 标记数组：记录每个节点的"反相版本"是否被使用过
    // 默认都是 false
    std::vector<bool> inverted_used(nodes.size(), false);

    // 1. 遍历所有 AND 门，检查其输入是否使用了反相信号
    for(size_t i = 1; i < nodes.size(); ++i) {
        const auto& n = nodes[i];
        if (n.is_input) continue;

        // 如果 fanin0 是反相的（奇数），标记 fanin0 对应的节点 ID 被反相使用了
        if (lit_inv(n.fanin0)) {
            inverted_used[lit_id(n.fanin0)] = true;
        }
        // 同理 fanin1
        if (lit_inv(n.fanin1)) {
            inverted_used[lit_id(n.fanin1)] = true;
        }
    }

    // 2. 遍历输出，检查输出是否直接引用了反相信号
    // (注意：之前的讨论提到有些工具不统计输出口的反相，
    //  但如果按照"物理反相器"逻辑，输出端若需要反相，也得算1个)
    for (uint32_t lit : outputs) {
        if (lit_inv(lit)) {
            inverted_used[lit_id(lit)] = true;
        }
    }

    // 3. 统计有多少个节点被标记了
    uint32_t cnt = 0;
    for (bool used : inverted_used) {
        if (used) cnt++;
    }
    
    return cnt;
}

void AigGraph::print_stats() const {
    std::cout << "pis=" << inputs.size()
              << ", pos=" << outputs.size()
              << ", area=" << countAnds()
              << ", depth=" << depth()
              << ", not=" << countInverters()
              << std::endl;
}