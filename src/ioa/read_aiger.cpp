#include "aig.h"
#include <fstream>
#include <vector>
#include <string>
#include <iostream>
#include <cassert>

// ---------------------------------------------------------------------
// 辅助函数：AIGER Literal -> Internal AigGraph Literal
// ---------------------------------------------------------------------
// aiger_lit: AIGER文件中的字面量 (偶数=原变量, 奇数=反相)
// table:     映射表 [AIGER_VAR_INDEX] -> Internal_Literal (Positive)
// ---------------------------------------------------------------------
static uint32_t resolve_lit(uint32_t aiger_lit, const std::vector<uint32_t>& table) {
    uint32_t var_idx = aiger_lit >> 1;
    bool is_inv = aiger_lit & 1;

    // table 存储的是该变量对应的 "正相" 内部 literal
    // 如果 aiger_lit 是奇数，我们需要对内部 literal 再取一次反
    // 利用 XOR 性质: lit ^ 1 相当于取反 (假设 lit 也是 LSB 为反相位的结构)
    return table[var_idx] ^ is_inv;
}

bool read_aiger_file(const std::string& filename, AigGraph& aig) {
    std::ifstream fin(filename);
    if (!fin) {
        std::cerr << "Error: Cannot open file " << filename << std::endl;
        return false;
    }

    std::string header;
    fin >> header;
    if (header != "aag") {
        std::cerr << "Error: Invalid header '" << header << "', expected 'aag'" << std::endl;
        return false;
    }

    uint32_t M, I, L, O, A;
    fin >> M >> I >> L >> O >> A;

    // -------------------------------------------------------
    // 映射表初始化
    // AIGER 变量索引从 0 到 M。
    // Index 0 固定为常量 False (对应内部 literal 0)
    // -------------------------------------------------------
    std::vector<uint32_t> aiger2lit(M + 1, 0); 

    // -------------------------------------------------------
    // 1. 读取 Inputs
    // -------------------------------------------------------
    for (uint32_t i = 0; i < I; ++i) {
        uint32_t lit;
        fin >> lit; // 读取 input literal (通常是偶数)
        
        uint32_t id = aig.addInput();
        // 记录映射: AIGER Var -> Internal Literal (make_lit(id, 0))
        aiger2lit[lit >> 1] = make_lit(id, false);
    }

    // -------------------------------------------------------
    // 2. 读取 Latches (作为伪输入 Pseudo-Inputs)
    // -------------------------------------------------------
    // 格式: "lhs next_state [reset]"
    // 我们只关心 lhs (当前状态输出)，将其视为电路的一个输入
    for (uint32_t i = 0; i < L; ++i) {
        uint32_t lhs;
        fin >> lhs;
        
        // 跳过这一行的剩余部分 (next_state 等)
        std::string dummy;
        std::getline(fin, dummy);

        uint32_t id = aig.addInput();
        aiger2lit[lhs >> 1] = make_lit(id, false);
    }

    // -------------------------------------------------------
    // 3. 读取 Outputs (先缓存)
    // -------------------------------------------------------
    // 注意：此时 Output 引用的 AND 门可能还没创建，所以不能直接 addOutput
    std::vector<uint32_t> output_lits(O);
    for (uint32_t i = 0; i < O; ++i) {
        fin >> output_lits[i];
    }

    // -------------------------------------------------------
    // 4. 读取 AND Gates
    // -------------------------------------------------------
    // AIGER 保证门是拓扑排序的，rhs 引用的变量一定已经定义过 (Input, Latch, 或之前的 AND)
    for (uint32_t i = 0; i < A; ++i) {
        uint32_t lhs, rhs0, rhs1;
        fin >> lhs >> rhs0 >> rhs1;

        // 解析右侧操作数
        uint32_t l0 = resolve_lit(rhs0, aiger2lit);
        uint32_t l1 = resolve_lit(rhs1, aiger2lit);

        // 构建 AND 节点
        // addAnd 会处理简单的常量折叠，并返回结果 Literal
        uint32_t res_lit = aig.addAnd(l0, l1);

        // 记录映射: AIGER Var (lhs) -> Result Literal
        aiger2lit[lhs >> 1] = res_lit;
    }

    // -------------------------------------------------------
    // 5. 连接 Outputs
    // -------------------------------------------------------
    for (uint32_t lit : output_lits) {
        aig.addOutput(resolve_lit(lit, aiger2lit));
    }

    // 解析成功 (忽略后续的 Symbol Table 和 Comments)
    return true;
}