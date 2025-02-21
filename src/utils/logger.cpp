// logger.cpp
#include "logger.h"
#include <filesystem>
#include <iostream>

bool Logger::initialize(const LoggerConfig& config) {
    config_ = config;
    
    try {
        // 创建日志目录
        std::filesystem::path log_path(config_.filename);
        std::filesystem::create_directories(log_path.parent_path());

        std::vector<spdlog::sink_ptr> sinks;

        // 添加控制台输出
        if (config_.console_output) {
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console_sink->set_pattern(config_.pattern);
            sinks.push_back(console_sink);
        }

        // 添加文件输出
        if (config_.daily_rotation) {
            auto file_sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(
                config_.filename, 0, 0);
            file_sink->set_pattern(config_.pattern);
            sinks.push_back(file_sink);
        } else {
            auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                config_.filename, config_.max_file_size, config_.max_files);
            file_sink->set_pattern(config_.pattern);
            sinks.push_back(file_sink);
        }

        logger_ = std::make_shared<spdlog::logger>("main", sinks.begin(), sinks.end());
        
        // 设置日志级别
        setLevel(config_.level);

        // 设置日志格式
        logger_->set_pattern(config_.pattern);

        // 设置全局默认logger
        spdlog::set_default_logger(logger_);
        
        // 设置flush策略
        spdlog::flush_every(std::chrono::seconds(3));

        return true;
    }
    catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Logger initialization failed: " << ex.what() << std::endl;
        return false;
    }
}

void Logger::setLevel(Level level) {
    if (!logger_) return;

    switch (level) {
        case Level::TRACE:
            logger_->set_level(spdlog::level::trace);
            break;
        case Level::DEBUG:
            logger_->set_level(spdlog::level::debug);
            break;
        case Level::INFO:
            logger_->set_level(spdlog::level::info);
            break;
        case Level::WARN:
            logger_->set_level(spdlog::level::warn);
            break;
        case Level::ERROR:
            logger_->set_level(spdlog::level::err);
            break;
        case Level::CRITICAL:
            logger_->set_level(spdlog::level::critical);
            break;
    }
}

void Logger::setPattern(const std::string& pattern) {
    if (!logger_) return;
    logger_->set_pattern(pattern);
}

std::shared_ptr<spdlog::logger> Logger::getLogger(const std::string& name) {
    auto it = loggers_.find(name);
    if (it != loggers_.end()) {
        return it->second;
    }

    try {
        std::vector<spdlog::sink_ptr> sinks;

        if (config_.console_output) {
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console_sink->set_pattern(config_.pattern);
            sinks.push_back(console_sink);
        }

        std::string filename = config_.filename;
        auto dot_pos = filename.find_last_of('.');
        if (dot_pos != std::string::npos) {
            filename.insert(dot_pos, "_" + name);
        }

        if (config_.daily_rotation) {
            auto file_sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(
                filename, 0, 0);
            file_sink->set_pattern(config_.pattern);
            sinks.push_back(file_sink);
        } else {
            auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                filename, config_.max_file_size, config_.max_files);
            file_sink->set_pattern(config_.pattern);
            sinks.push_back(file_sink);
        }

        auto logger = std::make_shared<spdlog::logger>(name, sinks.begin(), sinks.end());
        logger->set_level(logger_->level());
        loggers_[name] = logger;
        return logger;
    }
    catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Failed to create logger '" << name << "': " << ex.what() << std::endl;
        return nullptr;
    }
}