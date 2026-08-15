# 功能截图目录

本目录保存 C++ 多线程文件扫描器开发过程中经过验证的功能截图，并由根目录 README 引用。

## v2.0 截图

| 文件名 | 对应功能 | 捕获命令/操作 | 状态 |
|--------|----------|--------------|:--:|
| `17-v2-web-gui.png` | Web GUI 浏览器界面（玻璃拟态深色 UI + 环形饼图） | 启动 `file_scanner_web.exe`，Edge 无头截图 | ✅ |
| `09-v2-test-results.png` | 76 个单元测试全部通过 | `ctest --test-dir build --output-on-failure` | ⏳ |
| `10-v2-help.png` | v2.0 完整帮助信息 | `file_scanner.exe --help` | ⏳ |
| `11-v2-dupes.png` | 重复文件检测输出（含 NameAndSize 模式） | `file_scanner.exe . 4 --dupes` | ⏳ |
| `12-v2-json.png` | JSON 输出文件内容 | `file_scanner.exe . 4 --json output.json` | ⏳ |
| `13-v2-db-list.png` | 数据库历史扫描列表 | `file_scanner.exe --db scan.db --list-scans` | ⏳ |
| `14-v2-compare.png` | 扫描历史对比输出 | `file_scanner.exe . 4 --db scan.db --compare <id>` | ⏳ |
| `15-v2-gui-treemap.png` | Win32 GUI 扫描面板（含 macOS 风格改造） | 双击 `file_scanner_gui.exe` 扫描后截图 | ⏳ |
| `16-v2-cli-all.png` | 全功能综合运行 | 扫描 + CSV + JSON + DB + dupes 同时启用 | ⏳ |

## v1.0 截图（已完成）

| 文件名 | 对应功能 | 状态 |
|--------|----------|:--:|
| `01-build-success.png` | CMake + Ninja 构建成功 | ✅ |
| `02-basic-scan.png` | 基础目录扫描 | ✅ |
| `03-multithread-scan.png` | 多线程扫描和线程池回归 | ✅ |
| `08-gui-progress-theme-table.png` | Win32 GUI 进度条、主题和表格 | ✅ |
| `04-filtered-scan.png` | 扩展名、大小过滤和命令行配置 | ⏳ |
| `05-csv-export.png` | CSV 结果导出 | ⏳ |
| `06-performance-test.png` | 单线程/多线程性能对比 | ⏳ |
| `07-error-handling.png` | 非法路径和读取错误处理 | ⏳ |

## 截图规范

- PNG 格式，1600×900 或清晰裁剪
- 截图中不得出现账号、令牌、私人文件名等敏感信息
- 文件名使用英文小写、数字和短横线，按功能演进顺序编号
- 功能实现、编译、运行验证通过后再加入 README

## 更新流程

```
捕获截图 → 保存到本目录 → 更新本表 → 更新根目录 README 对应引用
```
