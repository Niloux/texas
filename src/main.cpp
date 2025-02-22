#include "audio_decoder.h"
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
  // 创建解码器配置
  AudioDecoderConfig config;
  config.maxQueueSize = 200;         // 设置更大的队列
  config.preBufferFrames = 20;       // 预缓冲20帧
  config.dropFramesWhenFull = false; // 队列满时阻塞

  // 创建解码器实例
  AudioDecoder decoder(config);

  // 打开音频文件
  if (decoder.open("/Users/wuyou/Downloads/sample-12s.mp3") !=
      AudioDecoderError::SUCCESS) {
    logger.error("Failed to open audio file");
    return -1;
  }

  // 获取音频参数
  int sampleRate = decoder.getSampleRate();
  int channels = decoder.getChannels();
  double duration = decoder.getDuration();
  logger.info("Audio file opened, sample rate: {}, channels: {}, duration: {}s",
              sampleRate, channels, duration);

  // 启动解码
  decoder.start();

  // 获取音频帧
  AVFrame *frame = nullptr;
  while (decoder.getAudioFrame(&frame, 1000)) {
    // 处理音频帧
    // ...
    logger.info("Decoded frame: {}", frame->nb_samples);
    // 释放音频帧
    av_frame_free(&frame);
  }

  decoder.stop();
  return 0;
}