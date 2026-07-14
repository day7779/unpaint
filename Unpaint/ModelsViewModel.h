#pragma once
#include "ModelsViewModel.g.h"
#include "ModelRepository.h"
#include "ModelDownloader.h"

namespace winrt::Unpaint::implementation
{
  struct ModelsViewModel : ModelsViewModelT<ModelsViewModel>
  {
    ModelsViewModel();

    fire_and_forget ImportModelFromHuggingFaceAsync();
    fire_and_forget ImportModelFromDiskAsync();

    Windows::Foundation::Collections::IObservableVector<ModelViewModel> RecommendedModels();
    int32_t SelectedRecommendedModel();
    void SelectedRecommendedModel(int32_t value);
    bool IsRecommendedModelSelected();
    void InstallSelectedRecommendedModelAsync();

    Windows::Foundation::Collections::IObservableVector<ModelViewModel> InstalledModels();
    bool AreInstalledModelsEmpty();
    
    int32_t SelectedInstalledModel();
    void SelectedInstalledModel(int32_t value);
    bool IsInstalledModelSelected();
    bool IsModelWebsiteAvailable();

    fire_and_forget OpenModelDirectory();
    void OpenInstalledModelWebsite();
    void CopyLinkToClipboard();
    fire_and_forget RemoveModelAsync();

    bool CanContinue();
    void Continue();

    fire_and_forget OpenUri(Windows::Foundation::Uri const& uri);
    fire_and_forget OpenControlNetSettingsAsync();

    event_token PropertyChanged(Windows::UI::Xaml::Data::PropertyChangedEventHandler const& value);
    void PropertyChanged(event_token const& token);

  private:
    static const char* const _modelFilter;
    INavigationService _navigationService;
    std::shared_ptr<ModelRepository> _modelRepository;
    event<Windows::UI::Xaml::Data::PropertyChangedEventHandler> _propertyChanged;

    Windows::Foundation::Collections::IObservableVector<ModelViewModel> _installedModels;
    int32_t _selectedInstalledModel = -1;

    Windows::Foundation::Collections::IObservableVector<ModelViewModel> _recommendedModels;
    int32_t _selectedRecommendedModel = -1;
    apartment_context _uiContext;

    void UpdateInstalledModels();
    fire_and_forget RefreshRecommendedModelsAsync();

    fire_and_forget DownloadHuggingFaceModelAsync(hstring const& modelId);
  };
}

namespace winrt::Unpaint::factory_implementation
{
  struct ModelsViewModel : ModelsViewModelT<ModelsViewModel, implementation::ModelsViewModel>
  {
  };
}
