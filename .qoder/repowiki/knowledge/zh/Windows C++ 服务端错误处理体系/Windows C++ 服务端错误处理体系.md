---
kind: error_handling
name: Windows C++ 服务端错误处理体系
category: error_handling
scope:
    - '**'
source_files:
    - SoundRemote/AudioUtil.h
    - SoundRemote/AudioUtil.cpp
    - SoundRemote/Server.cpp
    - SoundRemote/Util.h
    - SoundRemote/NetDefines.h
    - SoundRemote/UpdateChecker.h
    - SoundRemote/AudioCapture.cpp
---

## 1. 使用的系统与模式

该仓库采用**混合式错误处理策略**，根据调用栈层次和 API 类型选择不同方式：
- **Win32/COM HRESULT**：通过 `FAILED()` 宏判断，配合自定义辅助函数抛出异常或终止进程
- **Boost.Asio 异步 I/O**：使用 `boost::system::error_code` 回调参数 + `boost::asio::error::operation_aborted` 特殊处理取消
- **C++ 标准异常**：`std::runtime_error`、`Audio::Error`（继承自 `std::runtime_error`）用于业务层错误传播
- **网络协议级错误码**：`Net::Packet::Category::Error` 枚举值用于跨进程通信的错误类别
- **Windows 消息码**：`UPDATE_CHECK_ERROR` 等常量作为线程间结果通知
- **无 panic/recover**：未使用 C++ `panic` 语义，析构函数中仅展示错误不抛异常

## 2. 核心文件与位置

| 文件 | 职责 |
|------|------|
| `SoundRemote/AudioUtil.h/.cpp` | HRESULT→异常转换、错误文本生成、`EXIT_ON_ERROR`/`THROW_ON_ERROR` 宏 |
| `SoundRemote/Server.cpp` | 顶层协程错误捕获中心，统一 `showError` + `exit(EXIT_FAILURE)` |
| `SoundRemote/Util.h/.cpp` | 通用 UI 错误弹窗、错误文本格式化 (`makeAppErrorText`) |
| `SoundRemote/NetDefines.h` | 网络协议 `Category::Error` 定义 |
| `SoundRemote/UpdateChecker.h` | 更新检查返回码常量 (`UPDATE_CHECK_ERROR`) |
| `SoundRemote/AudioCapture.cpp` | COM 资源释放路径的析构错误处理示例 |

## 3. 架构与约定

### 3.1 HRESULT 错误处理分层
```cpp
// 宏封装：在函数内集中跳转清理
#define EXIT_ON_ERROR(hres)  if (FAILED(hres)) { goto Exit; }
#define THROW_ON_ERROR(hres, location) \
    if (FAILED(hres)) { hr = hres; where = location; goto Exit; }

// 工具函数：按场景选择行为
Audio::throwOnError(hr, Location::CAPTURE_AC_START);   // 抛 Audio::Error
Audio::exitOnError(hr, Location::UTIL_GETDEVICES_COINITIALIZE); // 弹窗+exit
```
`Location` 枚举为每个 COM 调用点提供细粒度定位，便于日志与诊断。

### 3.2 顶层异常兜底
`Server::receive()` 协程是唯一的全局 try/catch 块，捕获三类异常后统一展示并退出：
- `boost::system::system_error`：忽略 `operation_aborted`（正常关闭），其余视为致命错误
- `std::exception`：包装 `e.what()` 并通过 `Util::makeAppErrorText("Receive", ...)` 标注来源
- `...`：未知异常兜底

### 3.3 异步回调错误模型
Boost.Asio 回调使用 `error_code` 参数而非异常：
- `handleSend` / `maintain`：非零 ec 时 `throw std::runtime_error(...)`，由上层协程 catch
- `operation_aborted` 被显式识别为“预期取消”，不视为错误
- 定时器回调在 `ec != operation_aborted` 时抛异常，否则继续调度下一次超时

### 3.4 网络协议错误通道
`Net::Packet::Category::Error = 0` 保留给对端发送结构化错误；当前实现中尚未见具体构造逻辑，属于预留扩展点。

### 3.5 线程间结果传递
`UpdateChecker` 通过 Windows 消息 `WM_UPDATE_CHECK` 的 `wParam` 返回值区分成功/失败/错误，避免跨线程抛异常。

## 4. 开发者应遵循的规则

1. **Win32/COM 调用**：一律使用 `Audio::throwOnError` 或 `Audio::exitOnError`，禁止裸 `if(FAILED(hr))` 分支散落各处。需要局部清理时使用 `THROW_ON_ERROR` 宏配合 `goto Exit`。
2. **Boost.Asio 回调**：检查 `ec` 是否为 `boost::asio::error::operation_aborted`，非此值则 `throw std::runtime_error(Util::makeAppErrorText(<context>, ec.what()))`。
3. **顶层边界**：不要在业务层自行 `try/catch` 吞掉异常；让 `Server::receive` 统一兜底，确保所有未捕获异常都能弹出用户提示并安全退出。
4. **析构函数**：禁止抛异常，如需报告 COM 释放失败，改用 `Util::showError` 直接弹窗。
5. **跨线程通信**：使用返回值/消息码（如 `UPDATE_CHECK_*`），不在工作线程抛异常到 UI 线程。
6. **错误信息**：通过 `Util::makeAppErrorText(where, what)` 包裹上下文，保持日志可追溯性。