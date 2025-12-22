#include "aig.h"
#include <stdexcept>
#include <cstdint>
#include <algorithm>
#include <cassert>
#include <functional>

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
    if (lit0 == 0 || lit1 == 0) return 0;
    if (lit0 == 1) return lit1;
    if (lit1 == 1) return lit0;
    if (lit0 == lit1) return lit0;
    if (lit0 == (lit1 ^ 1)) return 0;

    if (lit0 > lit1) std::swap(lit0, lit1);

    // 1. 查表：如果这个 AND 门已经存在，直接返回旧的 ID
    uint64_t key = (static_cast<uint64_t>(lit0) << 32) | lit1;
    if (computed_table.count(key)) {
        return computed_table[key];
    }

    // 2. 检查 ID 是否越界 (安全性)
    uint32_t id0 = lit_id(lit0);
    uint32_t id1 = lit_id(lit1);
    if(id0 >= nodes.size() || id1 >= nodes.size())
            throw std::out_of_range("addAnd inputs invalid");

    // 3. 创建新节点
    uint32_t id = nodes.size();
    AigNode n;
    n.fanin0 = lit0;
    n.fanin1 = lit1;
    n.is_input = false;
    nodes.push_back(n);

    uint32_t res = make_lit(id, false);
    
    // 4. 记录到哈希表
    computed_table[key] = res;
    
    return res;
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
    std::unordered_map<uint64_t, uint32_t> strash; 
    
    // old2new 初始化为 UINT32_MAX，用来标记节点是否已被处理
    std::vector<uint32_t> old2new(nodes.size(), UINT32_MAX);

    // 1. 初始化常量 0
    new_nodes.push_back(nodes[0]); 
    old2new[0] = 0; 

    // 2. 优先处理 Inputs，保持输入顺序不变
    // (如果不这样做，递归可能会打乱 inputs 的索引顺序)
    std::vector<uint32_t> new_input_ids;
    for (uint32_t old_in_id : inputs) {
        uint32_t new_id = new_nodes.size();
        AigNode new_input_node;
        new_input_node.is_input = true;
        new_nodes.push_back(new_input_node);
        
        old2new[old_in_id] = make_lit(new_id, false);
        new_input_ids.push_back(new_id);
    }

    // 3. 定义递归函数：获取旧 Literal 对应的新 Literal
    // 注意：必须使用 std::function 以支持 lambda 递归
    std::function<uint32_t(uint32_t)> get_new_lit = 
        [&](uint32_t old_lit) -> uint32_t {
        
        uint32_t old_id = lit_id(old_lit);
        bool is_inv = lit_inv(old_lit);

        // 如果已经处理过，直接返回
        if (old2new[old_id] != UINT32_MAX) {
            return old2new[old_id] ^ is_inv;
        }

        // 如果没处理过，递归处理其子节点
        const AigNode& n = nodes[old_id];
        // 注意：Input 和 Constant 0 前面已经处理过了，理论上不会进到这里
        // 但为了安全起见或者处理游离节点：
        if (n.is_input || old_id == 0) {
             // 这种情况通常意味着逻辑错误，因为前面已经预先填充了 inputs
             throw std::runtime_error("Unexpected unmapped input/const");
        }

        uint32_t l0 = get_new_lit(n.fanin0);
        uint32_t l1 = get_new_lit(n.fanin1);

        // --- 常量传播与代数简化 (逻辑同你之前) ---
        uint32_t res;
        if (l0 == 0 || l1 == 0) { res = 0; }
        else if (l0 == 1) { res = l1; }
        else if (l1 == 1) { res = l0; }
        else if (l0 == l1) { res = l0; }
        else if (l0 == (l1 ^ 1)) { res = 0; }
        else {
            // --- Strashing ---
            if (l0 > l1) std::swap(l0, l1);
            uint64_t key = (static_cast<uint64_t>(l0) << 32) | l1;
            auto it = strash.find(key);
            if (it != strash.end()) {
                res = it->second;
            } else {
                uint32_t new_id = new_nodes.size();
                AigNode new_node;
                new_node.is_input = false;
                new_node.fanin0 = l0;
                new_node.fanin1 = l1;
                new_nodes.push_back(new_node);
                res = make_lit(new_id, false);
                strash[key] = res;
            }
        }

        // 记录映射结果
        old2new[old_id] = res;
        return res ^ is_inv;
    };

    // 4. 只从 Outputs 开始递归 (自动去除死逻辑 Dead Logic Elimination)
    std::vector<uint32_t> new_outputs;
    for (uint32_t old_out_lit : outputs) {
        new_outputs.push_back(get_new_lit(old_out_lit));
    }

    // 5. 更新图
    nodes.swap(new_nodes);
    inputs = new_input_ids; // inputs 已经是 ID 了
    outputs = new_outputs;
    
    // 【重要】清空 addAnd 用的哈希表，因为 ID 已经全变了
    computed_table.clear(); 
    // 将 strash 同步回去 下一轮 rewrite 调用 addAnd 时，能立即查到现有的节点
    for (auto const& [key, lit] : strash) {
        computed_table[key] = lit;
    }
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

// 检查是否存在 AND(lit0, lit1) 的节点
bool AigGraph::hasAnd(uint32_t lit0, uint32_t lit1) const {
    if (lit0 == 0 || lit1 == 0) return true; // Const 0 exists
    if (lit0 > lit1) std::swap(lit0, lit1);
    uint64_t key = (static_cast<uint64_t>(lit0) << 32) | lit1;
    return computed_table.count(key);
}

// 计算引用计数
std::vector<int> AigGraph::build_refs() const {
    std::vector<int> refs(nodes.size(), 0);
    // 遍历所有节点累加引用
    for (size_t i = 1; i < nodes.size(); ++i) {
        if (nodes[i].is_input) continue;
        refs[lit_id(nodes[i].fanin0)]++;
        refs[lit_id(nodes[i].fanin1)]++;
    }
    // 输出也是引用
    for (uint32_t out : outputs) {
        refs[lit_id(out)]++;
    }
    return refs;
}

// =============================================================
// Rewrite部分
// =============================================================


bool rewriteRedundant(uint32_t id, AigGraph& g, uint32_t& new_lit)
{
    const auto& n = g.nodes[id];
    if (n.is_input) return false;

    uint32_t x = n.fanin0;
    uint32_t y = n.fanin1;

    uint32_t xid = lit_id(x);
    uint32_t yid = lit_id(y);

    if (!g.nodes[xid].is_input) {
        auto& nx = g.nodes[xid];
        if (nx.fanin0 == y || nx.fanin1 == y) {
            new_lit = x;
            return true;
        }
    }

    if (!g.nodes[yid].is_input) {
        auto& ny = g.nodes[yid];
        if (ny.fanin0 == x || ny.fanin1 == x) {
            new_lit = y;
            return true;
        }
    }
    return false;
}

bool rewriteCommonFactor_P1(uint32_t id, AigGraph& g, const std::vector<int>& refs, uint32_t& new_lit)
{
    if (g.nodes[id].is_input) return false;

    // 1. 安全拷贝 (这是之前修好的部分)
    uint32_t x = g.nodes[id].fanin0;
    uint32_t y = g.nodes[id].fanin1;
    
    // 快速检查：如果 x 或 y 是输入，无法提取
    if (g.nodes[lit_id(x)].is_input || g.nodes[lit_id(y)].is_input) return false;

    // 拷贝孙子节点
    uint32_t xa = g.nodes[lit_id(x)].fanin0; 
    uint32_t xb = g.nodes[lit_id(x)].fanin1;
    uint32_t ya = g.nodes[lit_id(y)].fanin0;
    uint32_t yb = g.nodes[lit_id(y)].fanin1;

    // 2. 定义带代价评估的 pull 函数
    auto pull = [&](uint32_t c, uint32_t a, uint32_t b) {
        // --- 代价评估 (Heuristic) ---
        
        // 增益：如果原节点 x 或 y 引用计数为1，重写后它们将成为死节点 (Gain +1 each)
        // 注意：这里用 refs[id] 是不准的，我们要看 x 和 y 的 ref
        int gain = 0;
        if (refs[lit_id(x)] == 1) gain++;
        if (refs[lit_id(y)] == 1) gain++;

        // 代价：我们需要创建 t = AND(a, b) 和 res = AND(c, t)
        // 如果 t 已经存在，代价较小
        bool t_exists = g.hasAnd(a, b);
        int cost = (t_exists ? 0 : 1) + 1; // +1 是为了那个新的根节点 (new_lit)

        // 决策：只有当 增益 >= 代价 时才重写
        // 特例：如果只是单纯的结构调整（gain < cost），可能会导致 mem_ctrl 变差
        // 所以我们严格要求：
        if (gain < cost) return false;

        // --- 执行重写 ---
        uint32_t t = g.addAnd(a, b);   
        new_lit = g.addAnd(c, t);
        return true;
    };

    if (xa == ya) return pull(xa, xb, yb);
    if (xa == yb) return pull(xa, xb, ya);
    if (xb == ya) return pull(xb, xa, yb);
    if (xb == yb) return pull(xb, xa, ya);

    return false;
}

void AigGraph::rewrite_phase1()
{
    const uint32_t N = nodes.size();
    
    // 1. 预计算引用计数 (Static Reference Counting)
    // 虽然重写过程中引用会动态变化，但静态近似通常足够且高效
    std::vector<int> refs = build_refs();

    for (uint32_t id = 1; id < N; ++id) {
        if (nodes[id].is_input) continue;

        uint32_t new_lit;
        
        // 传入 refs
        if (rewriteCommonFactor_P1(id, *this, refs, new_lit))
        {
            nodes[id].fanin0 = new_lit;
            nodes[id].fanin1 = 1; 
            
            // 可选：在这里简单更新 refs，虽然对于 complex graph 不一定完全准确
            // 但对于单次 pass 来说，不更新也是为了防止连锁反应导致的震荡
        }
    }
}

bool rewriteNegAbsorb(uint32_t id, AigGraph& g,uint32_t& new_lit)
{
    const auto& n = g.nodes[id];
    if (n.is_input) return false;

    if (n.fanin0 == (n.fanin1 ^ 1) ||
        n.fanin1 == (n.fanin0 ^ 1)) {
        new_lit = 0;
        return true;
    }
    return false;
}

void AigGraph::rewrite_phase2()
{
    const uint32_t N = nodes.size();
    std::vector<uint32_t> replace(N, UINT32_MAX);

    for (uint32_t id = 1; id < N; ++id) {
        if (nodes[id].is_input) continue;

        uint32_t new_lit;
        if (rewriteNegAbsorb(id, *this, new_lit) ||
            rewriteRedundant(id, *this, new_lit) ||
            (nodes[id].fanin0 == nodes[id].fanin1 &&
             (new_lit = nodes[id].fanin0, true)))
        {
            replace[id] = new_lit;
        }
    }

    for (uint32_t id = 1; id < N; ++id) {
        auto& n = nodes[id];
        if (n.is_input) continue;

        if (replace[lit_id(n.fanin0)] != UINT32_MAX)
            n.fanin0 = replace[lit_id(n.fanin0)] ^ lit_inv(n.fanin0);

        if (replace[lit_id(n.fanin1)] != UINT32_MAX)
            n.fanin1 = replace[lit_id(n.fanin1)] ^ lit_inv(n.fanin1);
    }

    optimize();
}

void AigGraph::rewrite()
{
    for (int i = 0; i < 3; ++i) {
        rewrite_phase1();   // 制造结构
        optimize();         // strash 折叠
        rewrite_phase2();   // 真正减少 AND
    }
}