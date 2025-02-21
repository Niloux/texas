#include "logger.h"
#include <iostream>

int main(int argc, char **argv) {
  Logger::LoggerConfig config;
  config.filename = "logs/app.log";
  config.level = Logger::Level::DEBUG;

  // 初始化日志系统
  auto &logger = Logger::getInstance();
  if (!logger.initialize(config)) {
    std::cerr << "Failed to initialize logger" << std::endl;
    return -1;
  }

  logger.info("Application started");
  logger.debug("This is a debug message");
  logger.info("Application shutting down normally");
  return 0;
}
