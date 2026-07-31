// minitest main — 运行所有已注册的测试
#include "minitest.h"

int main() {
    int failed = minitest::RunAllTests();
    return failed > 0 ? 1 : 0;
}
