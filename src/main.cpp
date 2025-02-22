#include "audio_player.h"
#include "logger.h"
#include <iostream>

int main() {
  Logger::LoggerConfig lconfig;
  lconfig.filename = "logs/app.log";
  lconfig.level = Logger::Level::DEBUG;

  // 初始化日志系统
  auto &logger = Logger::getInstance();
  if (!logger.initialize(lconfig)) {
    std::cerr << "Failed to initialize logger" << std::endl;
    return -1;
  }

  logger.info("Application started");
  AudioPlayer player;
  if (player.loadFile("/Users/wuyou/Downloads/sample-12s.mp3")) {
    logger.info("File loaded successfully");
    player.play();

    logger.info("Playing... Press Enter to stop");
    std::cin.get();

    player.stop();
  } else {
    logger.error("Failed to load file");
  }
  logger.info("Application ended");
  return 0;
}