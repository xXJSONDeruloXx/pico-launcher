#include "common.h"
#include <array>
#include <string.h>
#include "core/mini-printf.h"
#include "picoLoaderBootstrap.h"
#include "PicoLoaderProcess.h"
#include "FileType/ExtensionFileTypeProvider.h"
#include "FileType/FileType.h"
#include "FileType/Nds/NdsFileType.h"
#include "SdFolderFactory.h"
#include "services/settings/IAppSettingsService.h"
#include "cheats/UsrCheatRepositoryFactory.h"
#include "cheats/EmptyCheatRepository.h"
#include "cheats/PicoLoaderCheatDataFactory.h"
#include "RomBrowserController.h"

static constexpr const char* sSaveStateArgumentPrefix = "__pico_state=";

static int findSaveSlotAssignmentIndex(const AppSettings& appSettings, const char* romPath)
{
    for (u32 i = 0; i < appSettings.numberOfSaveSlots; i++)
    {
        if (!strcmp(appSettings.saveSlots[i].romPath.GetString(), romPath))
            return i;
    }
    return -1;
}

static u32 getSaveSlotForPath(const AppSettings& appSettings, const char* romPath)
{
    int index = findSaveSlotAssignmentIndex(appSettings, romPath);
    if (index < 0)
        return 1;

    u32 saveSlot = appSettings.saveSlots[index].saveSlot;
    if (saveSlot == 0)
        return 1;
    if (saveSlot > 3)
        return 3;
    return saveSlot;
}

static void buildSavePath(const char* romPath, u32 saveSlot, char* savePath, u32 savePathLength)
{
    StringUtil::Copy(savePath, romPath, savePathLength);
    char* extension = strrchr(savePath, '.');
    if (!extension)
        extension = &savePath[strlen(savePath)];

    if (saveSlot <= 1)
    {
        extension[0] = '.';
        extension[1] = 's';
        extension[2] = 'a';
        extension[3] = 'v';
        extension[4] = 0;
    }
    else
    {
        mini_snprintf(extension, savePathLength - (extension - savePath), ".slot%d.sav", saveSlot);
    }
}

RomBrowserController::RomBrowserController(
    IAppSettingsService* appSettingsService, TaskQueueBase* ioTaskQueue,
    TaskQueueBase* bgTaskQueue)
    : _appSettingsService(appSettingsService)
    , _ioTaskQueue(ioTaskQueue), _bgTaskQueue(bgTaskQueue)
    , _fileTypeProvider(appSettingsService->GetAppSettings()) { }

void RomBrowserController::NavigateToPath(const TCHAR* name)
{
    StringUtil::Copy(_navigatePath, name, sizeof(_navigatePath) / sizeof(_navigatePath[0]));
    _stateMachine.Fire(RomBrowserStateTrigger::Navigate);
}

void RomBrowserController::LaunchFile(const FileInfo& fileInfo)
{
    _triggerFileInfo = FileInfo(fileInfo);
    _launchFromState = false;
    _stateMachine.Fire(RomBrowserStateTrigger::Launch);
}

void RomBrowserController::LaunchState(const FileInfo& fileInfo)
{
    _triggerFileInfo = FileInfo(fileInfo);
    _launchFromState = true;
    _stateMachine.Fire(RomBrowserStateTrigger::Launch);
}

void RomBrowserController::ShowGameInfo(const FileInfo& fileInfo)
{
    _triggerFileInfo = FileInfo(fileInfo);
    _stateMachine.Fire(RomBrowserStateTrigger::ShowGameInfo);
}

void RomBrowserController::ShowSaveSlots(const FileInfo& fileInfo)
{
    _triggerFileInfo = FileInfo(fileInfo);
    _stateMachine.Fire(RomBrowserStateTrigger::ShowSaveSlots);
}

void RomBrowserController::HideGameInfo()
{
    if (_saveSettingsPending)
    {
        _saveSettingsPending = false;
        _ioTaskQueue->Enqueue([this] (const vu8& cancelRequested)
        {
            _appSettingsService->Save();
            return TaskResult<void>::Completed();
        });
    }
    _stateMachine.Fire(RomBrowserStateTrigger::HideGameInfo);
}

void RomBrowserController::ShowDisplaySettings()
{
    _stateMachine.Fire(RomBrowserStateTrigger::ShowDisplaySettings);
}

void RomBrowserController::HideDisplaySettings()
{
    if (_saveSettingsPending)
    {
        _saveSettingsPending = false;
        _ioTaskQueue->Enqueue([this] (const vu8& cancelRequested)
        {
            _appSettingsService->Save();
            return TaskResult<void>::Completed();
        });
    }
    _stateMachine.Fire(RomBrowserStateTrigger::HideDisplaySettings);
}

void RomBrowserController::SetRomBrowserDisplaySettings(
    const RomBrowserDisplaySettings& romBrowserDisplaySettings)
{
    _appSettingsService->GetAppSettings().romBrowserDisplaySettings = romBrowserDisplaySettings;
    _saveSettingsPending = true;
    _stateMachine.Fire(RomBrowserStateTrigger::ChangeDisplayMode);
}

void RomBrowserController::Update()
{
    _stateMachine.Update();
    if (_stateMachine.HasStateChanged())
    {
        HandleTrigger();
    }
    switch (_stateMachine.GetCurrentState())
    {
        case RomBrowserState::Start:
        {
            LOG_DEBUG("RomBrowserState::Start\n");
            const auto& lastUsed = _appSettingsService->GetAppSettings().lastUsedFilePath;
            if (strlen(lastUsed.GetString()) != 0)
            {
                NavigateToPath(lastUsed.GetString());
            }
            else
            {
                NavigateToPath("/");
            }
            break;
        }
        case RomBrowserState::LoadingFolder:
        {
            if (_navigateTask.GetTask().IsCompletedSuccessfully())
            {
                _navigateTask.Dispose();
                _stateMachine.Fire(RomBrowserStateTrigger::FolderLoadDone);
            }
            break;
        }
        case RomBrowserState::Launching:
        default:
        {
            break;
        }
    }
}

void RomBrowserController::HandleTrigger()
{
    switch (_stateMachine.GetLastTrigger())
    {
        case RomBrowserStateTrigger::Navigate:
            HandleNavigateTrigger();
            break;

        case RomBrowserStateTrigger::FolderLoadDone:
            HandleFolderLoadDoneTrigger();
            break;

        case RomBrowserStateTrigger::Launch:
            HandleLaunchTrigger();
            break;

        case RomBrowserStateTrigger::ChangeDisplayMode:
            HandleChangeDisplayModeTrigger();
            break;

        default:
            break;
    }
}

void RomBrowserController::HandleNavigateTrigger()
{
    LOG_DEBUG("RomBrowserStateTrigger::Navigate\n");
    _navigateTask = _ioTaskQueue->Enqueue([this] (const vu8& cancelRequested)
    {
        if (!_coverRepository)
        {
            _coverRepository = std::make_unique<CoverRepository>();
            _coverRepository->Initialize();
        }
        if (!_cheatRepository)
        {
            _cheatRepository = UsrCheatRepositoryFactory().FromUsrCheatDat("/_pico/usrcheat.dat");
            if (!_cheatRepository)
            {
                // When usrcheat.dat is not found or cannot be read use a dummy empty cheat repository
                _cheatRepository = std::make_unique<EmptyCheatRepository>();
            }
        }

        u64 startTick = gTickCounter.GetValue();
        _navigateFileName = nullptr;
        if (strcmp(_navigatePath, "/") != 0) // can't f_stat on root dir
        {
            FILINFO fileInfo;
            if (f_stat(_navigatePath, &fileInfo) != FR_OK)
            {
                StringUtil::Copy(_navigatePath, "/", sizeof(_navigatePath) / sizeof(_navigatePath[0]));
            }
            else if (!(fileInfo.fattrib & AM_DIR))
            {
                _navigateFileName = strrchr(_navigatePath, '/') + 1;
                _navigateFileName[-1] = 0;
            }
        }
        f_chdir(_navigatePath);
        SdFolderFactory sdFolderFactory { &_fileTypeProvider };
        _newSdFolder = sdFolderFactory.CreateFromPath(".");
        u64 endTick = gTickCounter.GetValue();
        LOG_DEBUG("Loading files in folder took: %d us\n", (u32)TickCounter::TicksToMicroSeconds(endTick - startTick));
        return TaskResult<void>::Completed();
    });
}

void RomBrowserController::HandleFolderLoadDoneTrigger()
{
    LOG_DEBUG("RomBrowserStateTrigger::FolderLoadDone\n");
    _romBrowserViewModel.Reset();
    _sdFolder = std::move(_newSdFolder);
    _romBrowserViewModel = SharedPtr(new RomBrowserViewModel(this, _navigateFileName));
}

void RomBrowserController::HandleLaunchTrigger()
{
    LOG_DEBUG("RomBrowserStateTrigger::Launch\n");
    _ioTaskQueue->Enqueue([this] (const vu8& cancelRequested)
    {
        UpdateLastUsedFilepath();
        SetPicoLoaderParams();
        LoadCheats();
        return TaskResult<void>::Completed();
    });
}

void RomBrowserController::HandleChangeDisplayModeTrigger()
{
    LOG_DEBUG("RomBrowserStateTrigger::ChangeDisplayMode\n");
    _romBrowserViewModel = SharedPtr(new RomBrowserViewModel(this));
}

void RomBrowserController::UpdateLastUsedFilepath()
{
    f_getcwd(_navigatePath, sizeof(_navigatePath) / sizeof(_navigatePath[0]));
    int idx = strlcat(_navigatePath, "/", sizeof(_navigatePath));
    if (_navigatePath[idx - 2] == '/')
    {
        _navigatePath[idx - 1] = 0;
    }
    strlcat(_navigatePath, _triggerFileInfo.GetFileName(), sizeof(_navigatePath));
    _appSettingsService->GetAppSettings().lastUsedFilePath = _navigatePath;
    _appSettingsService->Save();
}

void RomBrowserController::SetPicoLoaderParams()
{
    auto loadParams = pload_getLoadParams();
    loadParams->savePath[0] = 0;
    loadParams->arguments[0] = 0;
    loadParams->argumentsLength = 0;
    if (_triggerFileInfo.GetFileType()->TrySetLaunchParameters(loadParams, _navigatePath))
    {
        if (_triggerFileInfo.GetFileType() == &NdsFileType::sInstance)
        {
            u32 saveSlot = getSaveSlotForPath(_appSettingsService->GetAppSettings(), loadParams->romPath);
            buildSavePath(loadParams->romPath, saveSlot, loadParams->savePath, sizeof(loadParams->savePath));

            if (_launchFromState)
            {
                char statePath[256];
                if (TryGetTriggerStatePath(statePath, sizeof(statePath)))
                {
                    FILINFO fileInfo;
                    if (f_stat(statePath, &fileInfo) == FR_OK)
                    {
                        mini_snprintf(loadParams->arguments, sizeof(loadParams->arguments), "%s%s", sSaveStateArgumentPrefix, statePath);
                        loadParams->argumentsLength = strlen(loadParams->arguments) + 1;
                    }
                }
            }
        }
        _launchFromState = false;
        gProcessManager.Goto<PicoLoaderProcess>();
    }
    else
    {
        _launchFromState = false;
        LOG_FATAL("Failed to set launch parameters.\n");
    }
}

u32 RomBrowserController::GetTriggerSaveSlot() const
{
    char filePath[256];
    if (!TryGetTriggerFilePath(filePath, sizeof(filePath)))
        return 1;

    return getSaveSlotForPath(_appSettingsService->GetAppSettings(), filePath);
}

void RomBrowserController::SetTriggerSaveSlot(u32 saveSlot)
{
    if (saveSlot > 3)
        saveSlot = 3;

    char filePath[256];
    if (!TryGetTriggerFilePath(filePath, sizeof(filePath)))
        return;

    auto& appSettings = _appSettingsService->GetAppSettings();
    int index = findSaveSlotAssignmentIndex(appSettings, filePath);
    if (saveSlot <= 1)
    {
        if (index >= 0)
        {
            std::unique_ptr<SaveSlotAssignment[]> saveSlots;
            if (appSettings.numberOfSaveSlots > 1)
            {
                saveSlots = std::make_unique_for_overwrite<SaveSlotAssignment[]>(appSettings.numberOfSaveSlots - 1);
                for (u32 i = 0, j = 0; i < appSettings.numberOfSaveSlots; i++)
                {
                    if ((int)i != index)
                    {
                        saveSlots[j++] = appSettings.saveSlots[i];
                    }
                }
            }
            appSettings.saveSlots = std::move(saveSlots);
            appSettings.numberOfSaveSlots--;
            _saveSettingsPending = true;
        }
        return;
    }

    if (index >= 0)
    {
        if (appSettings.saveSlots[index].saveSlot != saveSlot)
        {
            appSettings.saveSlots[index].saveSlot = saveSlot;
            _saveSettingsPending = true;
        }
        return;
    }

    auto saveSlots = std::make_unique_for_overwrite<SaveSlotAssignment[]>(appSettings.numberOfSaveSlots + 1);
    for (u32 i = 0; i < appSettings.numberOfSaveSlots; i++)
    {
        saveSlots[i] = appSettings.saveSlots[i];
    }
    saveSlots[appSettings.numberOfSaveSlots] = SaveSlotAssignment(filePath, saveSlot);
    appSettings.saveSlots = std::move(saveSlots);
    appSettings.numberOfSaveSlots++;
    _saveSettingsPending = true;
}

bool RomBrowserController::TryGetTriggerFilePath(char* filePath, u32 filePathLength) const
{
    if (f_getcwd(filePath, filePathLength) != FR_OK)
        return false;

    int idx = strlcat(filePath, "/", filePathLength);
    if (idx >= 2 && filePath[idx - 2] == '/')
    {
        filePath[idx - 1] = 0;
    }
    strlcat(filePath, _triggerFileInfo.GetFileName(), filePathLength);
    return true;
}

bool RomBrowserController::TryGetTriggerStatePath(char* filePath, u32 filePathLength) const
{
    if (!TryGetTriggerFilePath(filePath, filePathLength))
        return false;

    char* extension = strrchr(filePath, '.');
    if (!extension)
        extension = &filePath[strlen(filePath)];
    mini_snprintf(extension, filePathLength - (extension - filePath), ".state.bin");
    return true;
}

void RomBrowserController::LoadCheats() const
{
    auto cheats = _cheatRepository->GetCheatsForGame(_triggerFileInfo.GetFastFileRef());
    auto cheatData = PicoLoaderCheatDataFactory().CreateCheatData(cheats);
    pload_setCheatData(cheatData);
}
