#pragma once

#include "audio_decoder.h"
#include "logger.h"
#include <SDL2/SDL.h>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <vector>

class AudioPlayer {
public:
  // 播放器状态枚举
  enum class State { STOPPED, PLAYING, PAUSED };

  AudioPlayer();
  ~AudioPlayer();

  // 文件操作
  bool loadFile(const std::string &filename);   // 加载音频文件
  bool switchFile(const std::string &filename); // 切换到新的音频文件

  // 播放控制
  void play();               // 开始播放
  void pause();              // 暂停播放
  void resume();             // 恢复播放
  void stop();               // 停止播放
  void seek(double seconds); // 跳转到指定时间

  // 播放器控制
  void setVolume(int volume); // 设置音量 (0-128)
  int getVolume() const;      // 获取当前音量

  // 状态查询
  State getState() const;            // 获取当前播放状态
  double getCurrentPosition() const; // 获取当前播放位置（秒）
  double getDuration() const;        // 获取音频总时长（秒）

  // 音频格式信息
  int getSampleRate() const;
  int getChannels() const;

private:
  static void audioCallback(void *userdata, Uint8 *stream, int len);
  void fillAudioBuffer(Uint8 *stream, int len);
  bool pushAudioData(const uint8_t *data, int size);
  bool init(int sampleRate, int channels);
  void processDecodedFrame(AVFrame *frame);

  SDL_AudioDeviceID audioDevice;
  std::unique_ptr<AudioDecoder> decoder;
  State playerState;

  // 音频队列
  static const size_t MAX_QUEUE_SIZE = 50;
  std::queue<std::vector<uint8_t>> audioQueue;
  std::mutex queueMutex;
  std::condition_variable dataCondition;

  // 播放控制
  bool isPlaying;
  bool isPaused;
  int volume;
  double currentPosition; // 当前播放位置（秒）

  // 解码线程
  std::thread decodingThread;
  bool isDecodingThreadRunning;
  void decodingLoop();

  // 日志
  std::shared_ptr<spdlog::logger> _logger;
};