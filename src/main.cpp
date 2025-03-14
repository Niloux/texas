#include <iostream>
#include <string>
#include <thread>
#include <chrono>

#include "audio_player.h"
#include "logger.h"

// 显示菜单选项
void showMenu()
{
    std::cout << "\n===== Texas 音频播放器 =====\n";
    std::cout << "1. 加载音频文件\n";
    std::cout << "2. 播放\n";
    std::cout << "3. 暂停\n";
    std::cout << "4. 恢复\n";
    std::cout << "5. 停止\n";
    std::cout << "6. 跳转到指定时间\n";
    std::cout << "7. 调整音量\n";
    std::cout << "8. 显示当前状态\n";
    std::cout << "9. 退出\n";
    std::cout << "请输入选项 (1-9): ";
}

// 显示播放器当前状态
void showPlayerStatus(AudioPlayer &player)
{
    std::cout << "\n----- 播放器状态 -----\n";

    // 获取播放状态
    std::string stateStr;
    switch (player.getState())
    {
    case AudioPlayer::State::PLAYING:
        stateStr = "播放中";
        break;
    case AudioPlayer::State::PAUSED:
        stateStr = "已暂停";
        break;
    case AudioPlayer::State::STOPPED:
        stateStr = "已停止";
        break;
    }

    std::cout << "状态: " << stateStr << std::endl;
    std::cout << "当前位置: " << player.getCurrentPosition() << " 秒" << std::endl;
    std::cout << "总时长: " << player.getDuration() << " 秒" << std::endl;
    std::cout << "音量: " << player.getVolume() << " (0-128)" << std::endl;
    std::cout << "采样率: " << player.getSampleRate() << " Hz" << std::endl;
    std::cout << "声道数: " << player.getChannels() << std::endl;
    std::cout << "----------------------\n";
}

int main()
{
    // 配置日志系统
    Logger::LoggerConfig lconfig;
    lconfig.filename = "logs/app.log";
    lconfig.level = Logger::Level::DEBUG;
    lconfig.console_output = false;

    // 初始化日志系统
    auto &logger = Logger::getInstance();
    if (!logger.initialize(lconfig))
    {
        std::cerr << "Failed to initialize logger" << std::endl;
        return -1;
    }

    logger.info("Application started");

    // 创建播放器实例
    AudioPlayer player;
    std::string currentFile;
    bool isRunning = true;

    // 主循环
    while (isRunning)
    {
        showMenu();

        int choice;
        std::cin >> choice;
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // 清除输入缓冲区

        switch (choice)
        {
        case 1:
        { // 加载音频文件
            std::cout << "请输入音频文件路径: ";
            std::string filePath;
            std::getline(std::cin, filePath);

            logger.info("尝试加载文件: {}", filePath);
            if (player.loadFile(filePath))
            {
                logger.info("文件加载成功");
                currentFile = filePath;
                std::cout << "文件加载成功: " << filePath << std::endl;
            }
            else
            {
                logger.error("文件加载失败: {}", filePath);
                std::cout << "文件加载失败，请检查路径是否正确" << std::endl;
            }
            break;
        }

        case 2: // 播放
            if (currentFile.empty())
            {
                std::cout << "请先加载音频文件" << std::endl;
            }
            else
            {
                logger.info("开始播放");
                player.play();
                std::cout << "开始播放" << std::endl;
            }
            break;

        case 3: // 暂停
            logger.info("暂停播放");
            player.pause();
            std::cout << "已暂停" << std::endl;
            break;

        case 4: // 恢复
            logger.info("恢复播放");
            player.resume();
            std::cout << "已恢复播放" << std::endl;
            break;

        case 5: // 停止
            logger.info("停止播放");
            player.stop();
            std::cout << "已停止播放" << std::endl;
            break;

        case 6:
        { // 跳转到指定时间
            double seconds;
            std::cout << "请输入要跳转的时间点(秒): ";
            std::cin >> seconds;
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

            logger.info("跳转到 {} 秒", seconds);
            player.seek(seconds);
            std::cout << "已跳转到 " << seconds << " 秒" << std::endl;
            break;
        }

        case 7:
        { // 调整音量
            int volume;
            std::cout << "请输入音量大小 (0-128): ";
            std::cin >> volume;
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

            logger.info("设置音量: {}", volume);
            player.setVolume(volume);
            std::cout << "音量已设置为 " << player.getVolume() << std::endl;
            break;
        }

        case 8: // 显示当前状态
            showPlayerStatus(player);
            break;

        case 9: // 退出
            logger.info("用户请求退出应用");
            isRunning = false;
            break;

        default:
            std::cout << "无效选项，请重新输入" << std::endl;
            break;
        }

        // 短暂延迟，避免CPU占用过高
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // 确保停止播放
    player.stop();
    logger.info("Application ended");
    return 0;
}
