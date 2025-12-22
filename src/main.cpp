#include "aig.h"
#include <iostream>

int main(int argc, char** argv){
    if(argc<2){ std::cerr<<"Usage: "<<argv[0]<<" file.aag\n"; return 1; }

    AigGraph aig;
    if(!read_aiger_file(argv[1],aig)) return 1;

    // 优化前
    aig.print_stats();

    std::cout << "\noptimize\n\n";
    aig.rewrite();

    // 优化后
    aig.print_stats();

    return 0;
}
