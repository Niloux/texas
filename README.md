# Texas 音频播放器

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C++](https://img.shields.io/badge/language-C%2B%2B17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![xmake](https://img.shields.io/badge/build-xmake-green.svg)](https://xmake.io/)

Texas是一个基于FFmpeg和SDL2的高性能音频播放器，使用C++17开发，支持多种音频格式的解码和播放。

## 功能特性

- 支持多种音频格式（MP3, WAV, FLAC, AAC等）
- 高质量音频重采样
- 基本播放控制（播放、暂停、恢复、停止）
- 音量调节（0-128）
- 精确的音频定位（跳转）
- 缓冲区管理，防止音频卡顿
- 完善的日志系统，支持多级别日志和文件轮转

## 系统要求

- macOS, Linux 或 Windows
- C++17兼容的编译器
- xmake 构建工具

## 依赖项

- FFmpeg (用于音频解码)
- SDL2 (用于音频播放)
- spdlog (用于日志记录)

## 安装

### 安装依赖项

Texas使用xmake管理依赖项，xmake会自动下载并编译所需的依赖库。

首先，安装xmake：

```bash
# macOS (使用Homebrew)
brew install xmake

# Linux
wget https://xmake.io/shget.text -O - | bash

# Windows
下载并运行安装程序：https://github.com/xmake-io/xmake/releases
```

### 编译项目

```bash
# 克隆仓库
git clone https://github.com/yourusername/texas.git
cd texas

# 配置并构建项目
xmake config
xmake

# 运行
xmake run
```

## 使用方法

### 命令行界面

Texas提供了一个简单的命令行界面，可以通过以下命令启动：

```bash
xmake run
```

启动后，你可以使用以下命令：

1. 加载音频文件：输入文件路径
2. 播放：开始播放当前加载的音频文件
3. 暂停：暂停当前播放
4. 恢复：恢复暂停的播放
5. 停止：停止播放
6. 跳转：跳转到指定时间点（以秒为单位）
7. 音量：调整播放音量（0-128）
8. 状态：显示当前播放状态
9. 退出：退出程序

### 配置选项

#### 日志配置

日志配置位于`src/main.cpp`中，可以修改以下参数：

```cpp
Logger::LoggerConfig lconfig;
lconfig.filename = "logs/app.log";    // 日志文件路径
lconfig.level = Logger::Level::DEBUG;  // 日志级别
lconfig.console_output = false;        // 是否输出到控制台
```

#### 音频配置

音频解码器配置位于`include/audio_decoder.h`中：

```cpp
struct AudioDecoderConfig {
    size_t maxQueueSize = 100;        // 最大帧队列大小
    bool dropFramesWhenFull = false;  // 队列满时是否丢弃帧
    int timeoutMs = -1;               // 获取帧超时时间（毫秒）
};
```

## 错误处理和故障排除

### 常见错误

1. 文件加载失败
   - 检查文件路径是否正确
   - 确认文件格式是否支持
   - 查看日志文件获取详细错误信息

2. 音频播放问题
   - 检查系统音量设置
   - 确认音频设备是否正常工作
   - 查看日志中的音频设备初始化信息

3. 性能问题
   - 检查系统资源使用情况
   - 调整音频缓冲区大小
   - 考虑降低音频质量或重采样率

### 日志文件

日志文件默认保存在`logs/app.log`，包含详细的运行时信息和错误记录。

## 贡献指南

### 代码规范

1. 遵循C++17标准
2. 使用4空格缩进
3. 类名使用大驼峰命名法
4. 函数和变量使用小驼峰命名法
5. 常量使用全大写加下划线

### 提交规范

1. 提交信息格式：`<type>(<scope>): <subject>`
   - type: feat, fix, docs, style, refactor, test, chore
   - scope: 可选，表示修改范围
   - subject: 简短描述

2. 分支管理
   - main: 稳定版本
   - develop: 开发版本
   - feature/*: 新功能
   - bugfix/*: 错误修复

### 测试

1. 添加新功能时确保包含单元测试
2. 运行所有测试确保没有破坏现有功能
3. 测试覆盖率应保持在80%以上

## 开源许可

本项目采用MIT许可证。详见[LICENSE](LICENSE)文件。

## 更新日志

### v1.0.0
- 初始版本发布
- 支持基本的音频播放功能
- 实现命令行界面
- 添加日志系统