# 测试说明

本目录保存项目级 smoke test，用于在 Windows PowerShell 下验证命令行、扫描引擎、CSV、错误日志、性能脚本和 GUI 启动流程。

## 运行方式

先完成构建：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\.vscode\build.ps1"
```

再运行完整 smoke test：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\tests\smoke_tests.ps1"
```

如果只想验证 CLI、CSV、日志和性能，不启动 GUI：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\tests\smoke_tests.ps1" -SkipGui
```

## 覆盖范围

- 基础扫描和 1、4、64 线程结果一致性。
- 空目录和非法目录处理。
- 扩展名过滤与 CSV 行数校验。
- 错误日志生成和非法参数返回码。
- 1、2、4、8 线程性能脚本调用。
- Win32 GUI 启动、创建窗口和正常退出。

测试脚本会在 `output/` 下创建唯一临时目录，结束后只清理该目录；测试输出和性能 CSV 不进入 Git。
