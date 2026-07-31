# Changelog

## v2.0.0 (2026-07-31)

### 架构重构
- 三层 Clean Architecture：Domain / Application / Infrastructure
- 接口抽象：IScanner / IExporter / ILogger
- 构造函数依赖注入
- 命名空间重组：scanner::domain / scanner::application / scanner::infrastructure

### 新功能
- **重复文件检测**：三级 SHA256 策略（大小→64KB哈希→完整文件哈希）
- **SQLite 持久化**：扫描结果保存/加载/历史列表/差异对比
- **JSON 输出**：`--json` 选项，结构化格式
- **Treemap 可视化**：Win32 GUI 中按扩展名着色的大小区块图
- **CLI 全功能**：`--db` `--json` `--dupes` `--list-scans` `--compare`

### 工程质量
- 56 个单元测试（4 组，100% 通过），自包含测试框架
- GitHub Actions CI/CD：Windows / macOS / Linux 三平台
- .clang-format 代码规范（Google C++ Style）
- DLL 静态链接：exe 可独立运行，无需 MSYS2 bin 目录

### 修复
- MinGW 编译产物 DLL 依赖问题：使用 -static-libstdc++ -static-libgcc

---

## v1.0.0-demo (2026-07-25)

### 功能
- 递归目录扫描 + 有界线程池
- 扩展名/文件大小过滤
- 线程安全错误日志
- CSV 导出（UTF-8 BOM）
- 1/2/4/8 线程性能基准
- Win32 GUI（进度条/主题/扩展名表格）
- Smoke test（8 项全部通过）
- 完整项目文档和面试总结
