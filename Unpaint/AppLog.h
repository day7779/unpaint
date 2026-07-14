#pragma once
#include "pch.h"
#include <typeinfo>

namespace winrt::Unpaint
{
  class AppLog
  {
  public:
    static std::filesystem::path Path()
    {
      return std::filesystem::path{ Windows::Storage::ApplicationData::Current().LocalFolder().Path().c_str() } / L"unpaint.log";
    }

    static void Info(std::string_view stage, std::string_view message)
    {
      Write("INFO", stage, message);
    }

    static void Warning(std::string_view stage, std::string_view message)
    {
      Write("WARN", stage, message);
    }

    static void Error(std::string_view stage, std::string_view message)
    {
      Write("ERROR", stage, message);
    }

    static std::string DescribeException(std::exception_ptr exception = std::current_exception())
    {
      if (!exception) return "Unknown error.";

      try
      {
        std::rethrow_exception(exception);
      }
      catch (const hresult_error& error)
      {
        return std::format("winrt::hresult_error 0x{:08X}: {}", uint32_t(error.code().value), to_string(error.message()));
      }
      catch (const std::bad_alloc& error)
      {
        return std::format("std::bad_alloc: {}", error.what());
      }
      catch (const std::exception& error)
      {
        return std::format("{}: {}", typeid(error).name(), error.what());
      }
      catch (...)
      {
        return "Unknown native exception.";
      }
    }

  private:
    inline static std::mutex _mutex;

    static std::string Sanitize(std::string_view value)
    {
      std::string result{ value };
      std::replace(result.begin(), result.end(), '\r', ' ');
      std::replace(result.begin(), result.end(), '\n', ' ');
      return result;
    }

    static void Write(std::string_view level, std::string_view stage, std::string_view message)
    {
      try
      {
        SYSTEMTIME time{};
        GetSystemTime(&time);

        auto line = std::format(
          "{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z [{}] [{}] {}\r\n",
          time.wYear,
          time.wMonth,
          time.wDay,
          time.wHour,
          time.wMinute,
          time.wSecond,
          time.wMilliseconds,
          level,
          Sanitize(stage),
          Sanitize(message));

        OutputDebugStringA(line.c_str());

        std::lock_guard lock(_mutex);
        auto path = Path();
        std::error_code error;
        if (std::filesystem::exists(path, error) && std::filesystem::file_size(path, error) > 4ull * 1024 * 1024)
        {
          auto previousPath = path.parent_path() / L"unpaint.previous.log";
          std::filesystem::remove(previousPath, error);
          std::filesystem::rename(path, previousPath, error);
        }

        FILE* file = nullptr;
        if (_wfopen_s(&file, path.c_str(), L"ab") != 0 || !file) return;
        fwrite(line.data(), 1, line.size(), file);
        fclose(file);
      }
      catch (...)
      {
      }
    }
  };
}
