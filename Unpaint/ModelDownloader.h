#pragma once
#include "pch.h"
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>
#include <winrt/Windows.Web.Http.Filters.h>
#include <winrt/Windows.Storage.Streams.h>
#include "Json/JsonSerializer.h"
#include "Threading/AsyncOperation.h"

namespace winrt::Unpaint
{
  struct CuratedModelFile : public Axodox::Json::json_object_base
  {
    Axodox::Json::json_property<std::string> Path;
    Axodox::Json::json_property<std::string> Url;
    Axodox::Json::json_property<int64_t> Size;

    CuratedModelFile();
  };

  struct CuratedModelInfo : public Axodox::Json::json_object_base
  {
    Axodox::Json::json_property<std::string> Id;
    Axodox::Json::json_property<std::string> Name;
    Axodox::Json::json_property<std::string> Website;
    Axodox::Json::json_property<std::vector<CuratedModelFile>> Files;

    CuratedModelInfo();
  };

  struct CuratedModelManifest : public Axodox::Json::json_object_base
  {
    Axodox::Json::json_property<std::vector<CuratedModelInfo>> Models;

    CuratedModelManifest();
  };

  struct HuggingFaceSibling : public Axodox::Json::json_object_base
  {
    Axodox::Json::json_property<std::string> FilePath;
    Axodox::Json::json_property<int64_t> Size;

    HuggingFaceSibling();
  };

  struct HuggingFaceModelData : public Axodox::Json::json_object_base
  {
    Axodox::Json::json_property<std::string> Id;
    Axodox::Json::json_property<std::vector<HuggingFaceSibling>> Files;

    HuggingFaceModelData();
  };

  struct RemoteModelFile
  {
    std::string Path;
    std::wstring Url;
    int64_t Size;
  };

  class ModelDownloader
  {
  public:
    ModelDownloader();

    std::vector<CuratedModelInfo> GetCuratedModels();
    std::optional<CuratedModelInfo> GetCuratedModel(std::string_view modelId);

    bool ValidateModel(std::string_view modelId, std::string& status);
    bool TryDownloadModel(std::string_view modelId, const std::filesystem::path& targetPath, Axodox::Threading::async_operation& operation);

    static bool IsModelFileSetComplete(const std::set<std::string>& files, std::string* missingFiles = nullptr);

  private:
    static const std::set<std::string> _requiredFiles;
    static const std::set<std::string> _optionalFiles;
    static const char* const _manifestUri;
    static const char* const _fallbackManifest;

    winrt::Windows::Web::Http::HttpClient _httpClient;
    std::optional<std::vector<CuratedModelInfo>> _curatedModels;
    std::mutex _mutex;

    static winrt::Windows::Web::Http::HttpClient CreateClient();

    std::string TryQuery(std::wstring_view uri);
    std::optional<std::vector<RemoteModelFile>> GetHuggingFaceFiles(std::string_view modelId, std::string& error);
    bool TryDownloadFiles(const std::vector<RemoteModelFile>& files, const std::filesystem::path& targetPath, Axodox::Threading::async_operation_source& async);
    bool TryDownloadFile(const RemoteModelFile& file, const std::filesystem::path& targetFilePath, Axodox::Threading::async_operation_source& async, size_t fileIndex, size_t fileCount, std::string& error);
  };
}
