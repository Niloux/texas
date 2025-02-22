#include "audio_decoder.h"
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

  // AudioDecoder初始化
  AudioDecoder decoder;
  if (!decoder.open("/Users/wuyou/Downloads/sample-12s.mp3")) {
    logger.error("Failed to open audio file!");
    return -1;
  }

  decoder.start();

  AVFrame *frame;
  while (decoder.getAudioFrame(&frame)) {
    // 处理音频帧，通常是将 PCM 数据发送到音频播放模块
    logger.info("Decoded a frame");

    // 释放不再需要的帧
    av_frame_free(&frame);
  }

  decoder.stop();

  logger.info("Application started");
  logger.debug("This is a debug message");
  logger.info("Application shutting down normally");
  return 0;
}
