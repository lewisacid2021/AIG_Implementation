import os
import subprocess
import re
import sys

# ================= 配置区域 =================
# 可执行文件路径 (相对于脚本所在目录)
# 根据你的目录结构：build/bin/read_aig
BINARY_PATH = os.path.join("build", "bin", "read_aig")
# 测试用例根目录
TEST_DIR = "test"
# ===========================================

# ANSI 颜色代码，用于终端高亮
class Colors:
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'

# 用于解析输出行的正则表达式
# 格式: pis=14, pos=25, area=663, depth=15, not=513
STATS_PATTERN = re.compile(r"pis=(\d+),\s*pos=(\d+),\s*area=(\d+),\s*depth=(\d+),\s*not=(\d+)")

def parse_stats(text):
    """
    从文本中解析统计数据。
    如果有多行匹配，取最后一行（通常是优化后的结果）。
    返回字典: {'pis': int, 'pos': int, 'area': int, 'depth': int, 'not': int}
    """
    matches = STATS_PATTERN.findall(text)
    if not matches:
        return None
    
    # 取最后一次匹配的结果
    last_match = matches[-1]
    return {
        "pis": int(last_match[0]),
        "pos": int(last_match[1]),
        "area": int(last_match[2]),
        "depth": int(last_match[3]),
        "not": int(last_match[4])
    }

def run_test():
    # 检查二进制文件是否存在
    if not os.path.isfile(BINARY_PATH):
        print(f"{Colors.FAIL}Error: Binary not found at {BINARY_PATH}{Colors.ENDC}")
        print("Please build the project first.")
        sys.exit(1)

    print(f"{Colors.HEADER}Starting AIG Tests...{Colors.ENDC}")
    print(f"Binary: {BINARY_PATH}")
    print(f"Test Dir: {TEST_DIR}\n")

    failed_cases = []
    
    # 遍历目录
    for root, dirs, files in os.walk(TEST_DIR):
        for file in files:
            if file.endswith(".aag"):
                aag_path = os.path.join(root, file)
                txt_path = os.path.join(root, file.replace(".aag", ".txt"))
                
                # 打印文件名，保持光标在同一行等待结果
                print(f"Testing {Colors.BOLD}{file:<15}{Colors.ENDC} ... ", end="")

                # 1. 检查是否存在对应的 .txt 参考文件
                if not os.path.isfile(txt_path):
                    print(f"{Colors.WARNING}SKIP (No .txt ref){Colors.ENDC}")
                    continue

                # 2. 读取参考文件 (.txt)
                try:
                    with open(txt_path, 'r') as f:
                        ref_content = f.read()
                    ref_stats = parse_stats(ref_content)
                    if not ref_stats:
                        print(f"{Colors.WARNING}SKIP (Invalid .txt format){Colors.ENDC}")
                        continue
                except Exception as e:
                    print(f"{Colors.FAIL}ERROR reading .txt: {e}{Colors.ENDC}")
                    continue

                # 3. 运行 read_aig
                try:
                    # 运行程序并捕获输出
                    result = subprocess.run(
                        [BINARY_PATH, aag_path],
                        capture_output=True,
                        text=True,
                        timeout=5 # 设置5秒超时
                    )
                    
                    if result.returncode != 0:
                        print(f"{Colors.FAIL}CRASH (Return {result.returncode}){Colors.ENDC}")
                        if result.stderr:
                            print(f"  Stderr: {result.stderr.strip()}")
                        failed_cases.append((file, "Program Crashed or Failed"))
                        continue

                    my_output = result.stdout
                    my_stats = parse_stats(my_output)

                    if not my_stats:
                        print(f"{Colors.FAIL}FAIL (No stats in output){Colors.ENDC}")
                        # print(f"  Output: {my_output.strip()}")
                        failed_cases.append((file, "Output format parsing failed"))
                        continue

                except subprocess.TimeoutExpired:
                    print(f"{Colors.FAIL}TIMEOUT{Colors.ENDC}")
                    failed_cases.append((file, "Execution Timeout"))
                    continue
                except Exception as e:
                    print(f"{Colors.FAIL}EXEC ERROR: {e}{Colors.ENDC}")
                    continue

                # 4. 比较结果
                diffs = []
                
                # PIs / POs 必须相等
                if my_stats['pis'] != ref_stats['pis']:
                    diffs.append(f"PIs mismatch: {my_stats['pis']} != {ref_stats['pis']}")
                if my_stats['pos'] != ref_stats['pos']:
                    diffs.append(f"POs mismatch: {my_stats['pos']} != {ref_stats['pos']}")
                
                # Area, Depth, Not 不能比参考值差 (数值更大视为差)
                if my_stats['area'] > ref_stats['area']:
                    diffs.append(f"Area worse: {my_stats['area']} > {ref_stats['area']}")
                if my_stats['depth'] > ref_stats['depth']:
                    diffs.append(f"Depth worse: {my_stats['depth']} > {ref_stats['depth']}")
                if my_stats['not'] > ref_stats['not']:
                    diffs.append(f"Not worse: {my_stats['not']} > {ref_stats['not']}")

                # 格式化当前的统计结果字符串
                stats_str = (f"pis={my_stats['pis']}, pos={my_stats['pos']}, "
                             f"area={my_stats['area']}, depth={my_stats['depth']}, "
                             f"not={my_stats['not']}")

                # 5. 输出判定结果
                if not diffs:
                    is_exact = (my_stats == ref_stats)
                    if is_exact:
                        print(f"{Colors.OKGREEN}PASS{Colors.ENDC}  [{stats_str}]")
                    else:
                        print(f"{Colors.OKBLUE}PASS (Better){Colors.ENDC}  [{stats_str}]")
                else:
                    print(f"{Colors.FAIL}FAIL{Colors.ENDC}  [{stats_str}]")
                    for d in diffs:
                        print(f"  └─ {d}")
                        # 失败时也可以打印出参考值以便对比
                        # ref_str = f"pis={ref_stats['pis']}, pos={ref_stats['pos']}, area={ref_stats['area']}, depth={ref_stats['depth']}, not={ref_stats['not']}"
                        # print(f"     Ref: [{ref_str}]")

                    failed_cases.append((file, ", ".join(diffs)))

    # ================= 汇总报告 =================
    print("\n" + "="*40)
    print("Test Summary")
    print("="*40)
    if not failed_cases:
        print(f"{Colors.OKGREEN}All tests passed successfully!{Colors.ENDC}")
    else:
        print(f"{Colors.FAIL}Found {len(failed_cases)} issues:{Colors.ENDC}")
        for fname, reason in failed_cases:
            print(f"  - {fname:<15} : {reason}")
    print("="*40)

if __name__ == "__main__":
    run_test()