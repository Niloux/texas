#include "audio_player.h"
#include <algorithm>

AudioPlayer::AudioPlayer()
    : audioDevice(0), playerState(State::STOPPED), isPlaying(false),
      isPaused(false), volume(SDL_MIX_MAXVOLUME), currentPosition(0.0),
      isDecodingThreadRunning(false) {

  _logger = Logger::getInstance().getLogger("AudioPlayer");

  // 初始化SDL音频系统
  if (SDL_Init(SDL_INIT_AUDIO) < 0) {
    _logger->error("SDL初始化失败: {}", SDL_GetError());
    return;
  }

  // 创建解码器实例
  AudioDecoderConfig config;
  config.maxQueueSize = 50;
  config.dropFramesWhenFull = false;
  decoder = std::make_unique<AudioDecoder>(config);
}

AudioPlayer::~AudioPlayer() {
  stop();
  if (audioDevice) {
    SDL_CloseAudioDevice(audioDevice);
  }
  SDL_Quit();
}

bool AudioPlayer::loadFile(const std::string &filename) {
  stop(); // 停止当前播放

  // 打开新文件
  auto result = decoder->open(filename);
  if (result != AudioDecoderError::SUCCESS) {
    _logger->error("无法打开音频文件: {}", filename);
    return false;
  }

  // 初始化音频设备
  if (!init(decoder->getSampleRate(), decoder->getChannels())) {
    _logger->error("无法初始化音频设备");
    return false;
  }

  return true;
}

bool AudioPlayer::switchFile(const std::string &filename) {
  return loadFile(filename);
}

void AudioPlayer::play() {
  if (playerState == State::STOPPED) {
    if (!decoder) {
      _logger->error("没有加载音频文件");
      return;
    }

    isPlaying = true;
    playerState = State::PLAYING;

    // 启动解码线程
    isDecodingThreadRunning = true;
    decodingThread = std::thread(&AudioPlayer::decodingLoop, this);

    // 启动SDL音频设备
    SDL_PauseAudioDevice(audioDevice, 0);

    decoder->start();
  } else if (playerState == State::PAUSED) {
    resume();
  }
}

void AudioPlayer::pause() {
  if (playerState == State::PLAYING) {
    SDL_PauseAudioDevice(audioDevice, 1);
    playerState = State::PAUSED;
    isPaused = true;
  }
}

void AudioPlayer::resume() {
  if (playerState == State::PAUSED) {
    SDL_PauseAudioDevice(audioDevice, 0);
    playerState = State::PLAYING;
    isPaused = false;
  }
}

void AudioPlayer::stop() {
  if (playerState != State::STOPPED) {
    // 停止解码线程
    isDecodingThreadRunning = false;
    if (decodingThread.joinable()) {
      decodingThread.join();
    }

    // 停止音频设备
    SDL_PauseAudioDevice(audioDevice, 1);

    // 停止解码器
    if (decoder) {
      decoder->stop();
    }

    // 清空音频队列
    std::lock_guard<std::mutex> lock(queueMutex);
    std::queue<std::vector<uint8_t>> empty;
    audioQueue.swap(empty);

    playerState = State::STOPPED;
    isPlaying = false;
    isPaused = false;
    currentPosition = 0.0;
  }
}

void AudioPlayer::seek(double seconds) {
  if (!decoder)
    return;

  // 暂停音频输出
  SDL_PauseAudioDevice(audioDevice, 1);

  // 清空音频队列
  {
    std::lock_guard<std::mutex> lock(queueMutex);
    std::queue<std::vector<uint8_t>> empty;
    audioQueue.swap(empty);
  }

  // 执行seek操作
  if (decoder->seek(seconds)) {
    currentPosition = seconds;
  }

  // 如果之前在播放，恢复播放
  if (playerState == State::PLAYING) {
    SDL_PauseAudioDevice(audioDevice, 0);
  }
}

void AudioPlayer::setVolume(int vol) {
  volume = std::clamp(vol, 0, SDL_MIX_MAXVOLUME);
}

int AudioPlayer::getVolume() const { return volume; }

AudioPlayer::State AudioPlayer::getState() const { return playerState; }

double AudioPlayer::getCurrentPosition() const { return currentPosition; }

int AudioPlayer::getSampleRate() const {
  return decoder ? decoder->getSampleRate() : 0;
}

double AudioPlayer::getDuration() const {
  return decoder ? decoder->getDuration() : 0.0;
}

int AudioPlayer::getChannels() const {
  return decoder ? decoder->getChannels() : 0;
}

void AudioPlayer::decodingLoop() {
  AVFrame *frame = nullptr;

  while (isDecodingThreadRunning) {
    if (decoder->getAudioFrame(&frame, 100)) { // 100ms超时
      if (frame) {
        processDecodedFrame(frame);
        av_frame_free(&frame);
      }
    }
  }
}

void AudioPlayer::processDecodedFrame(AVFrame *frame) {
  // 计算每个采样的字节数
  int bytesPerSample = av_get_bytes_per_sample(decoder->getSampleFormat());
  int channels = decoder->getChannels();
  int totalBytes = frame->nb_samples * bytesPerSample * channels;

  // 创建临时缓冲区
  std::vector<uint8_t> buffer(totalBytes);
  uint8_t *dst = buffer.data();

  // 平面音频格式需要特殊处理
  if (av_sample_fmt_is_planar(decoder->getSampleFormat())) {
    for (int sample = 0; sample < frame->nb_samples; sample++) {
      for (int channel = 0; channel < channels; channel++) {
        memcpy(dst, frame->data[channel] + sample * bytesPerSample,
               bytesPerSample);
        dst += bytesPerSample;
      }
    }
  } else {
    memcpy(buffer.data(), frame->data[0], totalBytes);
  }

  // 更新当前播放位置
  if (frame->pts != AV_NOPTS_VALUE) {
    AVRational timeBase = decoder->getTimeBase();
    currentPosition = frame->pts * av_q2d(timeBase);
  }

  // 将处理后的数据推入播放队列
  if (!pushAudioData(buffer.data(), buffer.size())) {
    _logger->error("Failed to push audio data to queue");
  }
}

bool AudioPlayer::init(int sampleRate, int channels) {
  SDL_AudioSpec wanted_spec, obtained_spec;

  SDL_zero(wanted_spec);
  wanted_spec.freq = sampleRate;
  wanted_spec.format = AUDIO_S16SYS;
  wanted_spec.channels = channels;
  wanted_spec.samples = 4096;
  wanted_spec.callback = audioCallback;
  wanted_spec.userdata = this;

  if (audioDevice) {
    SDL_CloseAudioDevice(audioDevice);
  }

  audioDevice =
      SDL_OpenAudioDevice(nullptr, 0, &wanted_spec, &obtained_spec, 0);
  if (audioDevice == 0) {
    _logger->error("无法打开音频设备: {}", SDL_GetError());
    return false;
  }

  return true;
}

// SDL音频回调函数，当SDL需要更多音频数据时会调用这个函数
void AudioPlayer::audioCallback(void *userdata, Uint8 *stream, int len) {
  // 将userdata转换回AudioPlayer实例
  AudioPlayer *player = static_cast<AudioPlayer *>(userdata);
  // 调用实例方法填充音频缓冲区
  player->fillAudioBuffer(stream, len);
}

// 填充SDL音频缓冲区
void AudioPlayer::fillAudioBuffer(Uint8 *stream, int len) {
  // 清空音频缓冲区
  SDL_memset(stream, 0, len);

  // 如果暂停中，直接返回（会播放静音）
  if (isPaused) {
    return;
  }

  std::unique_lock<std::mutex> lock(queueMutex);

  while (len > 0 && !audioQueue.empty()) {
    // 获取队列中的第一个音频数据块
    std::vector<uint8_t> &data = audioQueue.front();

    // 计算本次要拷贝的数据长度
    int copyLen = std::min(len, static_cast<int>(data.size()));

    // 将数据混合到输出流中，使用设定的音量级别
    SDL_MixAudioFormat(stream, data.data(), AUDIO_S16SYS, copyLen, volume);

    if (copyLen < data.size()) {
      // 如果数据块没有完全使用，保留剩余部分
      data.erase(data.begin(), data.begin() + copyLen);
    } else {
      // 数据块已完全使用，从队列中移除
      audioQueue.pop();
    }

    // 更新剩余需要填充的长度
    stream += copyLen;
    len -= copyLen;
  }

  // 如果队列为空，通知可能在等待的生产者
  if (audioQueue.empty()) {
    lock.unlock();
    dataCondition.notify_one();
  }
}

// 将解码后的音频数据推入播放队列
bool AudioPlayer::pushAudioData(const uint8_t *data, int size) {
  if (!isPlaying || size <= 0) {
    return false;
  }

  std::unique_lock<std::mutex> lock(queueMutex);

  // 如果队列已满，等待队列有空间
  if (audioQueue.size() >= MAX_QUEUE_SIZE) {
    dataCondition.wait(lock, [this] {
      return audioQueue.size() < MAX_QUEUE_SIZE || !isPlaying;
    });

    // 如果在等待过程中播放器停止，返回false
    if (!isPlaying) {
      return false;
    }
  }

  // 创建新的数据块并拷贝数据
  std::vector<uint8_t> buffer(data, data + size);
  audioQueue.push(std::move(buffer));

  return true;
}