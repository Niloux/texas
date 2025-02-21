// logger.h
#pragma once
#include <memory>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <string>
#include <unordered_map>

class Logger {
public:
  enum class Level { TRACE, DEBUG, INFO, WARN, ERROR, CRITICAL };

  struct LoggerConfig {
    std::string filename;
    Level level;
    size_t max_file_size;
    size_t max_files;
    bool console_output;
    bool daily_rotation;
    std::string pattern;

    LoggerConfig()
        : filename("logs/app.log"), level(Level::INFO),
          max_file_size(10 * 1024 * 1024) // 10MB
          ,
          max_files(5), console_output(true), daily_rotation(false),
          pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] [%t] %v") {}
  };

  static Logger &getInstance() {
    static Logger instance;
    return instance;
  }

  bool initialize(const LoggerConfig &config = LoggerConfig());
  void setLevel(Level level);
  void setPattern(const std::string &pattern);

  template <typename... Args> void trace(const char *fmt, const Args &...args) {
    log(Level::TRACE, fmt, args...);
  }

  template <typename... Args> void debug(const char *fmt, const Args &...args) {
    log(Level::DEBUG, fmt, args...);
  }

  template <typename... Args> void info(const char *fmt, const Args &...args) {
    log(Level::INFO, fmt, args...);
  }

  template <typename... Args> void warn(const char *fmt, const Args &...args) {
    log(Level::WARN, fmt, args...);
  }

  template <typename... Args> void error(const char *fmt, const Args &...args) {
    log(Level::ERROR, fmt, args...);
  }

  template <typename... Args>
  void critical(const char *fmt, const Args &...args) {
    log(Level::CRITICAL, fmt, args...);
  }

  // 获取指定名称的子logger
  std::shared_ptr<spdlog::logger> getLogger(const std::string &name);

private:
  Logger() = default;
  ~Logger() = default;
  Logger(const Logger &) = delete;
  Logger &operator=(const Logger &) = delete;

  template <typename... Args>
  void log(Level level, const char *fmt, const Args &...args) {
    if (!logger_)
      return;

    switch (level) {
    case Level::TRACE:
      logger_->trace(fmt, args...);
      break;
    case Level::DEBUG:
      logger_->debug(fmt, args...);
      break;
    case Level::INFO:
      logger_->info(fmt, args...);
      break;
    case Level::WARN:
      logger_->warn(fmt, args...);
      break;
    case Level::ERROR:
      logger_->error(fmt, args...);
      break;
    case Level::CRITICAL:
      logger_->critical(fmt, args...);
      break;
    }
  }

  std::shared_ptr<spdlog::logger> logger_;
  std::unordered_map<std::string, std::shared_ptr<spdlog::logger>> loggers_;
  LoggerConfig config_;
};
