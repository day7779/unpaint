#include "pch.h"
#include "OnnxHost.h"
#include "AppLog.h"

using namespace Axodox::Infrastructure;
using namespace Axodox::MachineLearning::Executors;
using namespace Axodox::MachineLearning::Sessions;
using namespace Ort;
using namespace std;

namespace
{
  class PlatformDmlExecutor final : public OnnxExecutor
  {
  public:
    PlatformDmlExecutor(uint32_t adapterIndex) :
      _adapterIndex(adapterIndex)
    {
      ThrowOnError(GetApi().GetExecutionProviderApi("DML", ORT_API_VERSION, reinterpret_cast<const void**>(&_dmlApi)));
    }

    void ChangeAdapter(uint32_t adapterIndex)
    {
      lock_guard lock(_mutex);
      if (_adapterIndex == adapterIndex) return;

      _adapterIndex = adapterIndex;
      _events.raise(DeviceReset, this);
    }

    virtual void Ensure() override
    {
    }

    virtual void Apply(SessionOptions& sessionOptions) override
    {
      lock_guard lock(_mutex);
      ThrowOnError(_dmlApi->SessionOptionsAppendExecutionProvider_DML(sessionOptions, int(_adapterIndex)));
    }

  private:
    const OrtDmlApi* _dmlApi;
    recursive_mutex _mutex;
    uint32_t _adapterIndex;
  };

  shared_ptr<OnnxExecutor> create_executor(bool isXbox, uint32_t adapterIndex)
  {
    if (isXbox) return make_shared<PlatformDmlExecutor>(adapterIndex);
    return make_shared<DmlExecutor>(adapterIndex);
  }
}

namespace winrt::Unpaint
{
  OnnxHost::OnnxHost() :
    _state(dependencies.resolve<UnpaintState>()),
    _deviceInformation(dependencies.resolve<DeviceInformation>()),
    _environment(make_shared<OnnxEnvironment>()),
    _executor(create_executor(_deviceInformation->IsDeviceXbox(), *_state->AdapterIndex)),
    _adapterIndexChangedSubscription(_state->AdapterIndex.ValueChanged({ this, &OnnxHost::OnAdapterIndexChanged }))
  {
    AppLog::Info("DirectML", format("Using {} DirectML provider on adapter {}.", _deviceInformation->IsDeviceXbox() ? "platform" : "custom", *_state->AdapterIndex));
  }

  const std::shared_ptr<Axodox::MachineLearning::Sessions::OnnxEnvironment>& OnnxHost::Environment() const
  {
    return _environment;
  }

  const std::shared_ptr<Axodox::MachineLearning::Executors::OnnxExecutor>& OnnxHost::Executor() const
  {
    return _executor;
  }

  Axodox::MachineLearning::Sessions::OnnxSessionParameters OnnxHost::ParametersFromFile(const std::filesystem::path& path) const
  {
    return { _environment, _executor, OnnxModelSource::FromFilePath(path) };
  }

  Axodox::MachineLearning::Sessions::OnnxSessionParameters OnnxHost::ParametersFromFile(const winrt::Windows::Storage::StorageFile& file) const
  {
    return { _environment, _executor, OnnxModelSource::FromStorageFile(file) };
  }

  void OnnxHost::OnAdapterIndexChanged(OptionPropertyBase*)
  {
    AppLog::Info("DirectML", format("Changing adapter to {}.", *_state->AdapterIndex));
    if (_deviceInformation->IsDeviceXbox()) static_pointer_cast<PlatformDmlExecutor>(_executor)->ChangeAdapter(*_state->AdapterIndex);
    else static_pointer_cast<DmlExecutor>(_executor)->ChangeAdapter(*_state->AdapterIndex);
  }
}
