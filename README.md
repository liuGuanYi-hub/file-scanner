# C++ 多线程文件扫描器

一个面向 C++ 开发实习准备的 Windows 命令行工具项目，用于练习 C++17、STL、文件系统、多线程、线程同步、CMake 和工程化文档。

## 项目状态

当前版本为基础可运行版，已经完成：

- 递归扫描目录
- 多线程文件处理
- 线程安全任务队列
- 独立 `ThreadPool` 线程池模块
- 文件数量、总大小和错误数统计
- 文件扩展名统计
- C++17 + CMake + Ninja 构建
- 基础构建和运行截图

计划中的过滤、日志、CSV 导出和性能基准将在 10 天开发计划中逐步完成。

详细计划见：[docs/开发计划.md](docs/开发计划.md)

## 技术栈

- C++17
- STL：`std::filesystem`、`std::thread`、`std::mutex`、`std::condition_variable`、`std::atomic`
- MSYS2 UCRT64 GCC 16.1.0
- CMake 4.4.0
- Ninja 1.13.2
- GDB
- VS Code

## 功能演示

### 构建成功

![CMake 和 Ninja 构建成功](docs/screenshots/01-build-success.png)

### 基础扫描

![基础目录扫描结果](docs/screenshots/02-basic-scan.png)

### 线程池回归验证

![线程池多线程扫描结果](docs/screenshots/03-multithread-scan.png)

## 项目结构

```text
.
├── .vscode/
├── docs/
│   ├── 开发计划.md
│   └── screenshots/
│       ├── README.md
│       ├── 01-build-success.png
│       ├── 02-basic-scan.png
│       └── 03-multithread-scan.png
├── include/
│   └── thread_pool.h
├── src/
│   ├── main.cpp
│   └── thread_pool.cpp
├── CMakeLists.txt
├── README.md
└── .gitignore
```

## 编译

当前项目使用安装在 `F:\msys64` 的 UCRT64 GCC、CMake 和 Ninja。

```powershell
$env:PATH = "F:\msys64\ucrt64\bin;F:\msys64\usr\bin;$env:PATH"
cmake -G Ninja -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## 运行

扫描当前目录：

```powershell
.\build\file_scanner.exe . 4
```

扫描指定目录：

```powershell
.\build\file_scanner.exe "D:\资料" 8
```

参数说明：

1. 第一个参数是扫描目录，省略时扫描当前目录。
2. 第二个参数是工作线程数，省略时根据 CPU 自动选择，范围为 1 到 64。

## 技术实现

主线程负责遍历目录并将文件路径放入线程安全队列；工作线程从队列中取出文件，读取文件大小并统计扩展名。队列关闭后，工作线程会处理完剩余任务再退出。

当前实现重点体现：

- 生产者-消费者模型
- 独立线程池和通用 `enqueue()` 任务提交接口
- 互斥锁和条件变量
- 原子计数器
- `std::filesystem` 错误码处理
- Windows 宽字符命令行入口和 UTF-8 路径显示
- CMake 可复现构建

## 测试结果

基础验证命令：

```powershell
cmake -G Ninja -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
.\build\file_scanner.exe . 4
```

当前基础验证结果：

- 编译成功
- 程序正常退出
- 扫描文件数和扩展名统计正常
- 错误数为 0
- 多线程参数可用
- 中文非法目录能够返回可读错误并以退出码 1 结束
- 1、4、64 线程扫描结果一致

## 开发计划与截图规范

- [专业开发计划](docs/开发计划.md)
- [功能截图目录说明](docs/screenshots/README.md)

每完成一个功能，按照以下顺序更新项目：

```text
实现功能
→ 编译验证
→ 运行验证
→ 保存截图
→ 更新截图说明
→ 更新 README
```

## 后续路线

1. 增加任务队列容量限制和优雅退出。
2. 增加扩展名、大小和时间过滤。
3. 增加日志、CSV 导出和配置参数。
4. 增加单线程/多线程性能对比。
5. 后续扩展 GUI、数据库、网络和 Windows API 能力。
