#include "audio_player.h"
#include <algorithm>
#include <numeric>

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
  if (swrContext) {
    swr_free(&swrContext);
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

  // 初始化重采样器
  if (!initResampler()) {
    _logger->error("无法初始化重采样器");
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

    // 清理重采样器
    if (swrContext) {
      swr_free(&swrContext);
      swrContext = nullptr;
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
  if (!frame) {
    _logger->error("Null frame received");
    return;
  }

  if (!swrContext) {
    _logger->error("Resampler not initialized");
    return;
  }

  auto start = std::chrono::high_resolution_clock::now();

  try {
    // 计算输出样本数，考虑重采样延迟
    int64_t delay = swr_get_delay(swrContext, frame->sample_rate);
    int out_samples =
        av_rescale_rnd(delay + frame->nb_samples, deviceSampleRate,
                       frame->sample_rate, AV_ROUND_UP);

    // 检查输出样本数是否合理
    if (out_samples <= 0) {
      _logger->error("Invalid output samples count: {}", out_samples);
      return;
    }

    // 分配输出缓冲区，添加额外的安全空间
    size_t bytesPerSample = sizeof(int16_t);
    size_t bufferSize = out_samples * deviceChannels * bytesPerSample;
    std::vector<uint8_t> buffer(bufferSize);

    if (buffer.empty()) {
      _logger->error("Failed to allocate buffer of size: {}", bufferSize);
      return;
    }

    uint8_t *output_buffer[1] = {buffer.data()};

    // 执行重采样
    int samples_out =
        swr_convert(swrContext, output_buffer, out_samples,
                    (const uint8_t **)frame->extended_data, frame->nb_samples);

    if (samples_out < 0) {
      char errbuf[AV_ERROR_MAX_STRING_SIZE];
      av_strerror(samples_out, errbuf, AV_ERROR_MAX_STRING_SIZE);
      _logger->error("Resampling error: {}", errbuf);
      return;
    }

    // 更新播放位置
    if (frame->pts != AV_NOPTS_VALUE) {
      AVRational timeBase = decoder->getTimeBase();
      double newPosition = frame->pts * av_q2d(timeBase);

      // 检测是否有大的时间跳变
      if (std::abs(newPosition - currentPosition) > 0.1) { // 100ms以上的跳变
        _logger->debug("Time jump detected: {} -> {}", currentPosition,
                       newPosition);
      }

      currentPosition = newPosition;
    }

    // 计算实际输出的字节数
    size_t actualBufferSize = samples_out * deviceChannels * bytesPerSample;

    // 性能监控
    auto end = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    if (duration.count() > 1000) { // 超过1ms的处理时间
      _logger->warn("Frame processing took {} us, samples: {}, size: {} bytes",
                    duration.count(), samples_out, actualBufferSize);
    }

    // 监控重采样比率
    float resampleRatio = static_cast<float>(samples_out) / frame->nb_samples;
    if (std::abs(resampleRatio - 1.0f) > 0.1f) { // 重采样比率偏差超过10%
      _logger->debug("High resample ratio: {:.2f}, in: {}, out: {}",
                     resampleRatio, frame->nb_samples, samples_out);
    }

    // 将处理后的数据推入播放队列
    std::unique_lock<std::mutex> lock(queueMutex);

    // 检查队列大小
    if (audioQueue.size() >= MAX_QUEUE_SIZE) {
      _logger->warn("Queue full, waiting for space...");
      dataCondition.wait(lock, [this] {
        return audioQueue.size() < MAX_QUEUE_SIZE || !isDecodingThreadRunning;
      });

      if (!isDecodingThreadRunning) {
        _logger->debug("Decoding thread stopped while waiting");
        return;
      }
    }

    // 创建新的数据块并移动数据
    std::vector<uint8_t> data;
    data.reserve(actualBufferSize); // 预分配内存避免复制
    data.assign(buffer.data(), buffer.data() + actualBufferSize);

    audioQueue.push(std::move(data));
    updateBufferSize(actualBufferSize);

    // 如果队列之前接近空，记录恢复事件
    if (audioQueue.size() <= LOW_WATER_MARK) {
      _logger->debug("Buffer recovering: {} frames in queue",
                     audioQueue.size());
    }

  } catch (const std::exception &e) {
    _logger->error("Exception in processDecodedFrame: {}", e.what());
  } catch (...) {
    _logger->error("Unknown exception in processDecodedFrame");
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

  // 保存实际获得的音频参数
  deviceFormat = obtained_spec.format;
  deviceChannels = obtained_spec.channels;
  deviceSampleRate = obtained_spec.freq;

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
  SDL_memset(stream, 0, len);

  std::unique_lock<std::mutex> lock(queueMutex);

  if (audioQueue.empty()) {
    underrun = true;
    _logger->warn("Audio buffer underrun detected");
    return;
  }

  size_t totalCopied = 0;
  while (len > 0 && !audioQueue.empty()) {
    std::vector<uint8_t> &data = audioQueue.front();
    int copyLen = std::min(len, static_cast<int>(data.size()));

    if (underrun && totalCopied == 0) {
      for (int i = 0; i < copyLen; i++) {
        float fade = static_cast<float>(i) / copyLen;
        stream[i] = static_cast<Uint8>(data[i] * fade);
      }
    } else {
      SDL_MixAudioFormat(stream, data.data(), AUDIO_S16SYS, copyLen, volume);
    }

    if (copyLen < data.size()) {
      data.erase(data.begin(), data.begin() + copyLen);
      updateBufferSize(-copyLen);
    } else {
      updateBufferSize(-data.size());
      audioQueue.pop();
    }

    stream += copyLen;
    len -= copyLen;
    totalCopied += copyLen;
  }

  if (audioQueue.size() < LOW_WATER_MARK) {
    lock.unlock();
    dataCondition.notify_one();
  }

  underrun = false;
}

void AudioPlayer::updateBufferSize(int64_t delta) {
  size_t current = bufferedSize.load();
  while (!bufferedSize.compare_exchange_weak(current, current + delta))
    ; // 空循环直到成功
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

bool AudioPlayer::initResampler() {
  if (!decoder) {
    _logger->error("Decoder is not initialized");
    return false;
  }

  if (swrContext) {
    swr_free(&swrContext);
  }

  // 创建重采样上下文
  swrContext = swr_alloc();
  if (!swrContext) {
    _logger->error("Could not allocate resampler context");
    return false;
  }

  // 获取输入通道数和采样格式
  int in_channels = decoder->getChannels();
  int in_sample_rate = decoder->getSampleRate();
  AVSampleFormat in_sample_fmt = decoder->getSampleFormat();

  // 创建输入和输出通道布局
  AVChannelLayout in_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
  AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;

  if (in_channels == 1) {
    in_ch_layout = AV_CHANNEL_LAYOUT_MONO;
  }

  _logger->debug("Initializing resampler:");
  _logger->debug("Input: channels={}, rate={}, format={}", in_channels,
                 in_sample_rate, av_get_sample_fmt_name(in_sample_fmt));

  // 设置输入参数 - 注意这里修改了调用方式
  int ret = swr_alloc_set_opts2(&swrContext,
                                &out_ch_layout,    // 输出通道布局
                                AV_SAMPLE_FMT_S16, // 输出采样格式
                                deviceSampleRate,  // 输出采样率
                                &in_ch_layout,     // 输入通道布局
                                in_sample_fmt,     // 输入采样格式
                                in_sample_rate,    // 输入采样率
                                0,                 // 日志偏移
                                nullptr            // 日志上下文
  );

  if (ret < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
    _logger->error("Could not allocate resampler context: {}", errbuf);
    return false;
  }

  // 初始化重采样器
  ret = swr_init(swrContext);
  if (ret < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
    _logger->error("Failed to initialize resampler: {}", errbuf);
    return false;
  }

  _logger->info("Resampler initialized successfully");
  return true;
}