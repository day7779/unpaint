#include "pch.h"
#include "ModelDownloader.h"
#include "AppLog.h"

#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>
#include <winrt/Windows.Web.Http.Filters.h>
#include <winrt/Windows.Storage.Streams.h>

using namespace Axodox::Infrastructure;
using namespace Axodox::Json;
using namespace Axodox::Threading;
using namespace std;
using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Storage::Streams;
using namespace winrt::Windows::Web::Http;
using namespace winrt::Windows::Web::Http::Filters;

namespace winrt::Unpaint
{
  const char* const ModelDownloader::_manifestUri = "https://raw.githubusercontent.com/day7779/unpaint/main/models.json";

  const char* const ModelDownloader::_fallbackManifest = R"JSON({
    "models": [
      {
        "id": "curated/counterfeit-v3",
        "name": "Counterfeit V3 (anime)",
        "website": "https://huggingface.co/gsdf/Counterfeit-V3.0",
        "files": [
          { "path": "unet/model.onnx", "url": "https://github.com/day7779/unpaint/releases/download/models-v1/counterfeit-v3.unet.model.onnx", "size": 0 },
          { "path": "text_encoder/model.onnx", "url": "https://github.com/day7779/unpaint/releases/download/models-v1/counterfeit-v3.text_encoder.model.onnx", "size": 0 },
          { "path": "vae_decoder/model.onnx", "url": "https://github.com/day7779/unpaint/releases/download/models-v1/counterfeit-v3.vae_decoder.model.onnx", "size": 0 },
          { "path": "vae_encoder/model.onnx", "url": "https://github.com/day7779/unpaint/releases/download/models-v1/counterfeit-v3.vae_encoder.model.onnx", "size": 0 },
          { "path": "tokenizer/vocab.json", "url": "https://github.com/day7779/unpaint/releases/download/models-v1/counterfeit-v3.tokenizer.vocab.json", "size": 0 },
          { "path": "tokenizer/merges.txt", "url": "https://github.com/day7779/unpaint/releases/download/models-v1/counterfeit-v3.tokenizer.merges.txt", "size": 0 },
          { "path": "tokenizer/special_tokens_map.json", "url": "https://github.com/day7779/unpaint/releases/download/models-v1/counterfeit-v3.tokenizer.special_tokens_map.json", "size": 0 },
          { "path": "tokenizer/tokenizer_config.json", "url": "https://github.com/day7779/unpaint/releases/download/models-v1/counterfeit-v3.tokenizer.tokenizer_config.json", "size": 0 },
          { "path": "scheduler/scheduler_config.json", "url": "https://github.com/day7779/unpaint/releases/download/models-v1/counterfeit-v3.scheduler.scheduler_config.json", "size": 0 }
        ]
      }
    ]
  })JSON";

  const std::set<std::string> ModelDownloader::_requiredFiles = {
    "scheduler/scheduler_config.json",
    "text_encoder/model.onnx",
    "tokenizer/merges.txt",
    "tokenizer/special_tokens_map.json",
    "tokenizer/tokenizer_config.json",
    "tokenizer/vocab.json",
    "unet/model.onnx",
    "vae_decoder/model.onnx"
  };

  const std::set<std::string> ModelDownloader::_optionalFiles = {
    "feature_extractor/preprocessor_config.json",
    "safety_checker/model.onnx",
    "vae_encoder/model.onnx",
    "controlnet/model.onnx",
    "text_encoder_2/model.onnx",
    "text_encoder_2/model.onnx.data",
    "tokenizer_2/merges.txt",
    "tokenizer_2/special_tokens_map.json",
    "tokenizer_2/tokenizer_config.json",
    "tokenizer_2/vocab.json",
    "unet/model.onnx.data"
  };

  CuratedModelFile::CuratedModelFile() :
    Path(this, "path"),
    Url(this, "url"),
    Size(this, "size")
  { }

  CuratedModelInfo::CuratedModelInfo() :
    Id(this, "id"),
    Name(this, "name"),
    Website(this, "website"),
    Files(this, "files")
  { }

  CuratedModelManifest::CuratedModelManifest() :
    Models(this, "models")
  { }

  HuggingFaceSibling::HuggingFaceSibling() :
    FilePath(this, "rfilename"),
    Size(this, "size")
  { }

  HuggingFaceModelData::HuggingFaceModelData() :
    Id(this, "id"),
    Files(this, "siblings")
  { }

  ModelDownloader::ModelDownloader() :
    _httpClient(CreateClient())
  { }

  winrt::Windows::Web::Http::HttpClient ModelDownloader::CreateClient()
  {
    HttpBaseProtocolFilter filter{};
    filter.AllowUI(false);

    auto cacheControl = filter.CacheControl();
    cacheControl.ReadBehavior(HttpCacheReadBehavior::MostRecent);
    cacheControl.WriteBehavior(HttpCacheWriteBehavior::NoCache);

    HttpClient client{ filter };
    client.DefaultRequestHeaders().UserAgent().TryParseAdd(L"unpaint/2.0");
    return client;
  }

  std::vector<CuratedModelInfo> ModelDownloader::GetCuratedModels()
  {
    {
      lock_guard lock(_mutex);
      if (_curatedModels) return *_curatedModels;
    }

    auto manifestText = TryQuery(to_wstring(_manifestUri));
    auto manifest = try_parse_json<CuratedModelManifest>(manifestText);
    if (!manifest || manifest->Models->empty())
    {
      AppLog::Warning("Model download", "Could not fetch the curated model manifest, using the built-in list.");
      manifest = try_parse_json<CuratedModelManifest>(_fallbackManifest);
    }

    lock_guard lock(_mutex);
    _curatedModels = manifest ? *manifest->Models : vector<CuratedModelInfo>{};
    return *_curatedModels;
  }

  std::optional<CuratedModelInfo> ModelDownloader::GetCuratedModel(std::string_view modelId)
  {
    for (auto& model : GetCuratedModels())
    {
      if (*model.Id == modelId) return model;
    }

    return nullopt;
  }

  bool ModelDownloader::IsModelFileSetComplete(const std::set<std::string>& files, std::string* missingFiles)
  {
    string missing;
    for (auto& file : _requiredFiles)
    {
      if (!files.contains(file))
      {
        if (!missing.empty()) missing += ", ";
        missing += file;
      }
    }

    if (missingFiles) *missingFiles = missing;
    return missing.empty();
  }

  bool ModelDownloader::ValidateModel(std::string_view modelId, std::string& status)
  {
    if (GetCuratedModel(modelId))
    {
      status = "";
      return true;
    }

    string error;
    auto files = GetHuggingFaceFiles(modelId, error);
    if (!files)
    {
      status = error;
      return false;
    }

    status = "";
    return true;
  }

  bool ModelDownloader::TryDownloadModel(std::string_view modelId, const std::filesystem::path& targetPath, Axodox::Threading::async_operation& operation)
  {
    async_operation_source async;
    operation.set_source(async);

    AppLog::Info("Model download", std::format("Installing model {}...", modelId));

    //Collect the files to download
    vector<RemoteModelFile> files;

    auto curatedModel = GetCuratedModel(modelId);
    if (curatedModel)
    {
      for (auto& file : *curatedModel->Files)
      {
        files.push_back(RemoteModelFile{ *file.Path, to_wstring(*file.Url), *file.Size });
      }
    }
    else
    {
      async.update_state(NAN, "Fetching model metadata...");

      string error;
      auto huggingFaceFiles = GetHuggingFaceFiles(modelId, error);
      if (!huggingFaceFiles)
      {
        AppLog::Error("Model download", std::format("Installing model {} failed: {}", modelId, error));
        async.update_state(async_operation_state::failed, error);
        return false;
      }

      files = move(*huggingFaceFiles);
    }

    //Ensure output directory
    error_code ec;
    if (!filesystem::exists(targetPath, ec))
    {
      filesystem::create_directories(targetPath, ec);
      if (ec)
      {
        async.update_state(async_operation_state::failed, "Failed to create the model directory.");
        return false;
      }
    }

    //Download files
    auto result = TryDownloadFiles(files, targetPath, async);

    if (result)
    {
      AppLog::Info("Model download", std::format("Model {} installed successfully.", modelId));
      async.update_state(async_operation_state::succeeded, 1.f, "Model downloaded successfully.");
    }

    return result;
  }

  std::string ModelDownloader::TryQuery(std::wstring_view uri)
  {
    try
    {
      HttpRequestMessage request{};
      request.RequestUri(Uri{ uri });
      request.Method(HttpMethod::Get());
      request.Headers().Accept().TryParseAdd(L"application/json");

      auto requestResult = _httpClient.TrySendRequestAsync(request).get();
      if (!requestResult.Succeeded()) return "";

      auto response = requestResult.ResponseMessage();
      if (response.StatusCode() != HttpStatusCode::Ok) return "";

      return to_string(response.Content().ReadAsStringAsync().get());
    }
    catch (...)
    {
      AppLog::Error("Model download", std::format("Querying {} failed: {}", to_string(uri), AppLog::DescribeException()));
      return "";
    }
  }

  std::optional<std::vector<RemoteModelFile>> ModelDownloader::GetHuggingFaceFiles(std::string_view modelId, std::string& error)
  {
    auto metadataText = TryQuery(to_wstring(std::format("https://huggingface.co/api/models/{}?blobs=true", modelId)));
    auto metadata = try_parse_json<HuggingFaceModelData>(metadataText);
    if (!metadata)
    {
      error = "Failed to fetch model metadata from HuggingFace.";
      return nullopt;
    }

    set<string> availableFiles;
    for (auto& file : *metadata->Files)
    {
      availableFiles.emplace(*file.FilePath);
    }

    string missing;
    if (!IsModelFileSetComplete(availableFiles, &missing))
    {
      error = std::format("The model does not match the Stable Diffusion ONNX layout, missing: {}.", missing);
      return nullopt;
    }

    vector<RemoteModelFile> results;
    for (auto& file : *metadata->Files)
    {
      if (!_requiredFiles.contains(*file.FilePath) && !_optionalFiles.contains(*file.FilePath)) continue;

      results.push_back(RemoteModelFile{
        *file.FilePath,
        to_wstring(std::format("https://huggingface.co/{}/resolve/main/{}", modelId, *file.FilePath)),
        *file.Size
      });
    }

    return results;
  }

  bool ModelDownloader::TryDownloadFiles(const std::vector<RemoteModelFile>& files, const std::filesystem::path& targetPath, Axodox::Threading::async_operation_source& async)
  {
    error_code ec;
    auto fileCount = files.size();
    size_t fileIndex = 0;

    for (auto& file : files)
    {
      if (async.is_cancelled()) break;

      //Ensure folder
      auto targetFilePath = (targetPath / file.Path).make_preferred();
      auto targetFolderPath = targetFilePath.parent_path();
      if (!filesystem::exists(targetFolderPath, ec))
      {
        filesystem::create_directories(targetFolderPath, ec);
        if (ec)
        {
          async.update_state(async_operation_state::failed, "Failed to create the model directory.");
          return false;
        }
      }

      //Skip files which are already complete
      if (file.Size > 0 && filesystem::exists(targetFilePath, ec) && int64_t(filesystem::file_size(targetFilePath, ec)) == file.Size)
      {
        fileIndex++;
        continue;
      }

      //Download file
      string error;
      if (!TryDownloadFile(file, targetFilePath, async, fileIndex, fileCount, error))
      {
        if (!async.is_cancelled())
        {
          auto message = std::format("Failed to download {}: {}", file.Path, error);
          AppLog::Error("Model download", message);
          async.update_state(async_operation_state::failed, message);
        }
        else
        {
          async.update_state(async_operation_state::cancelled, 1.f, "Operation cancelled.");
        }

        return false;
      }

      fileIndex++;
    }

    if (async.is_cancelled())
    {
      async.update_state(async_operation_state::cancelled, 1.f, "Operation cancelled.");
      return false;
    }

    return true;
  }

  bool ModelDownloader::TryDownloadFile(const RemoteModelFile& file, const std::filesystem::path& targetFilePath, Axodox::Threading::async_operation_source& async, size_t fileIndex, size_t fileCount, std::string& error)
  {
    error_code ec;
    auto partialFilePath = targetFilePath;
    partialFilePath += L".partial";

    for (auto attempt = 0; attempt < 3 && !async.is_cancelled(); attempt++)
    {
      try
      {
        //Resume the download if a partial file exists
        int64_t existingSize = filesystem::exists(partialFilePath, ec) ? int64_t(filesystem::file_size(partialFilePath, ec)) : 0;

        HttpRequestMessage request{};
        request.RequestUri(Uri{ file.Url });
        request.Method(HttpMethod::Get());
        if (existingSize > 0)
        {
          request.Headers().TryAppendWithoutValidation(L"Range", to_hstring(std::format("bytes={}-", existingSize)));
        }

        auto requestResult = _httpClient.TrySendRequestAsync(request, HttpCompletionOption::ResponseHeadersRead).get();
        if (!requestResult.Succeeded())
        {
          error = "The connection failed.";
          continue;
        }

        auto response = requestResult.ResponseMessage();
        auto statusCode = response.StatusCode();

        if (statusCode == HttpStatusCode::Unauthorized || statusCode == HttpStatusCode::Forbidden)
        {
          error = std::format("HTTP {} - the host has blocked anonymous downloads of this file (storage quota or gated model). Please select another model source.", int32_t(statusCode));
          return false;
        }

        bool append;
        if (statusCode == HttpStatusCode::PartialContent && existingSize > 0) append = true;
        else if (statusCode == HttpStatusCode::Ok) { append = false; existingSize = 0; }
        else
        {
          error = std::format("HTTP {}", int32_t(statusCode));
          continue;
        }

        //Determine the expected file size
        auto expectedSize = file.Size;
        auto content = response.Content();
        auto contentLength = content.Headers().ContentLength();
        if (expectedSize <= 0 && contentLength)
        {
          expectedSize = existingSize + int64_t(contentLength.Value());
        }

        //Copy to disk
        ofstream target{ partialFilePath, (append ? ios::app : ios::trunc) | ios::binary };
        if (!target.is_open())
        {
          error = "Failed to open the output file.";
          return false;
        }

        auto sourceStream = content.ReadAsInputStreamAsync().get();

        int64_t position = existingSize;
        Buffer buffer{ 1024 * 1024 };
        while (!async.is_cancelled())
        {
          auto bufferRead = sourceStream.ReadAsync(buffer, buffer.Capacity(), InputStreamOptions::None).get();
          if (bufferRead.Length() == 0) break;

          target.write(reinterpret_cast<const char*>(bufferRead.data()), bufferRead.Length());
          position += bufferRead.Length();

          auto fileProgress = expectedSize > 0 ? float(double(position) / double(expectedSize)) : 0.f;
          async.update_state((float(fileIndex) + fileProgress) / float(fileCount), std::format("Downloading {} ({}/{} MB)...", file.Path, position / 1024 / 1024, expectedSize / 1024 / 1024));
        }

        sourceStream.Close();
        target.close();

        if (async.is_cancelled()) return false;

        //Verify the downloaded size
        if (expectedSize > 0 && position != expectedSize)
        {
          error = std::format("the download ended too early ({}/{} bytes)", position, expectedSize);
          AppLog::Warning("Model download", std::format("Downloading {} was truncated on attempt {} ({}/{} bytes), retrying...", file.Path, attempt + 1, position, expectedSize));
          continue;
        }

        filesystem::remove(targetFilePath, ec);
        filesystem::rename(partialFilePath, targetFilePath, ec);
        if (ec)
        {
          error = "Failed to finalize the downloaded file.";
          return false;
        }

        return true;
      }
      catch (...)
      {
        error = AppLog::DescribeException();
        AppLog::Warning("Model download", std::format("Downloading {} failed on attempt {}: {}", file.Path, attempt + 1, error));
      }
    }

    return false;
  }
}
