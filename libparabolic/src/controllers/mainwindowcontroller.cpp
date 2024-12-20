﻿#include "controllers/mainwindowcontroller.h"
#include <format>
#include <sstream>
#include <thread>
#include <libnick/filesystem/userdirectories.h>
#include <libnick/helpers/stringhelpers.h>
#include <libnick/localization/documentation.h>
#include <libnick/localization/gettext.h>
#include <libnick/system/environment.h>
#include "models/configuration.h"
#include "models/downloadhistory.h"
#include "models/downloadrecoveryqueue.h"
#include "models/previousdownloadoptions.h"
#ifdef _WIN32
#include <windows.h>
#endif

using namespace Nickvision::App;
using namespace Nickvision::Events;
using namespace Nickvision::Filesystem;
using namespace Nickvision::Helpers;
using namespace Nickvision::Keyring;
using namespace Nickvision::Localization;
using namespace Nickvision::Notifications;
using namespace Nickvision::System;
using namespace Nickvision::TubeConverter::Shared::Events;
using namespace Nickvision::TubeConverter::Shared::Models;
using namespace Nickvision::Update;

namespace Nickvision::TubeConverter::Shared::Controllers
{
    MainWindowController::MainWindowController(const std::vector<std::string>& args)
        : m_started{ false },
        m_args{ args },
        m_appInfo{ "org.nickvision.tubeconverter", "Nickvision Parabolic", "Parabolic" },
        m_dataFileManager{ m_appInfo.getName() },
        m_logger{ UserDirectories::get(ApplicationUserDirectory::LocalData, m_appInfo.getName()) / "log.txt", Logging::LogLevel::Info, false },
        m_keyring{ m_appInfo.getId() },
        m_downloadManager{ m_dataFileManager.get<Configuration>("config").getDownloaderOptions(), m_dataFileManager.get<DownloadHistory>("history"), m_dataFileManager.get<DownloadRecoveryQueue>("recovery"), m_logger }
    {
        m_appInfo.setVersion({ "2024.11.1" });
        m_appInfo.setShortName(_("Parabolic"));
        m_appInfo.setDescription(_("Download web video and audio"));
        m_appInfo.setChangelog("- Fixed an issue where file names that included a period were truncated\n- Fixed an issue where long file names caused the application to crash\n- Fixed an issue where external subtitle files were created although embedding was supported\n- Fixed an issue where generic downloads were unable to be opened upon completion\n- Updated yt-dlp to 2024.11.18");
        m_appInfo.setSourceRepo("https://github.com/NickvisionApps/Parabolic");
        m_appInfo.setIssueTracker("https://github.com/NickvisionApps/Parabolic/issues/new");
        m_appInfo.setSupportUrl("https://github.com/NickvisionApps/Parabolic/discussions");
        m_appInfo.setHtmlDocsStore(m_appInfo.getVersion().getVersionType() == VersionType::Stable ? "https://github.com/NickvisionApps/Parabolic/blob/" + m_appInfo.getVersion().str() + "/docs/html" : "https://github.com/NickvisionApps/Parabolic/blob/main/docs/html");
        m_appInfo.getExtraLinks()[_("Matrix Chat")] = "https://matrix.to/#/#nickvision:matrix.org";
        m_appInfo.getDevelopers()["Nicholas Logozzo"] = "https://github.com/nlogozzo";
        m_appInfo.getDevelopers()[_("Contributors on GitHub")] = "https://github.com/NickvisionApps/Parabolic/graphs/contributors";
        m_appInfo.getDesigners()["Nicholas Logozzo"] = "https://github.com/nlogozzo";
        m_appInfo.getDesigners()[_("Fyodor Sobolev")] = "https://github.com/fsobolev";
        m_appInfo.getDesigners()["DaPigGuy"] = "https://github.com/DaPigGuy";
        m_appInfo.getArtists()[_("David Lapshin")] = "https://github.com/daudix";
        m_appInfo.setTranslatorCredits(_("translator-credits"));
        Localization::Gettext::init(m_appInfo.getEnglishShortName());
#ifdef _WIN32
        m_updater = std::make_shared<Updater>(m_appInfo.getSourceRepo());
#endif
        m_dataFileManager.get<Configuration>("config").saved() += [this](const EventArgs&){ onConfigurationSaved(); };
        //Log information
        if(!m_keyring.isSavingToDisk())
        {
            m_logger.log(Logging::LogLevel::Warning, "Keyring not being saved to disk.");
        }
        if(m_dataFileManager.get<Configuration>("config").getPreventSuspend())
        {
            if(m_suspendInhibitor.inhibit())
            {
                m_logger.log(Logging::LogLevel::Info, "Inhibited system suspend.");
            }
            else
            {
                m_logger.log(Logging::LogLevel::Error, "Unable to inhibit system suspend.");
            }
        }
    }

    const AppInfo& MainWindowController::getAppInfo() const
    {
        return m_appInfo;
    }

    DownloadManager& MainWindowController::getDownloadManager()
    {
        return m_downloadManager;
    }

    Theme MainWindowController::getTheme()
    {
        return m_dataFileManager.get<Configuration>("config").getTheme();
    }

    void MainWindowController::setShowDisclaimerOnStartup(bool showDisclaimerOnStartup)
    {
        Configuration& config{ m_dataFileManager.get<Configuration>("config") };
        config.setShowDisclaimerOnStartup(showDisclaimerOnStartup);
        config.save();
    }

    Event<EventArgs>& MainWindowController::configurationSaved()
    {
        return m_dataFileManager.get<Configuration>("config").saved();
    }

    Event<NotificationSentEventArgs>& MainWindowController::notificationSent()
    {
        return m_notificationSent;
    }

    Event<ShellNotificationSentEventArgs>& MainWindowController::shellNotificationSent()
    {
        return m_shellNotificationSent;
    }

    Event<ParamEventArgs<std::string>>& MainWindowController::disclaimerTriggered()
    {
        return m_disclaimerTriggered;
    }

    std::string MainWindowController::getDebugInformation(const std::string& extraInformation) const
    {
        std::stringstream builder;
        //yt-dlp
        if(Environment::findDependency("yt-dlp").empty())
        {
            builder << "yt-dlp not found" << std::endl;
        }
        else
        {
            std::string ytdlpVersion{ Environment::exec(Environment::findDependency("yt-dlp").string() + " --version") };
            builder << "yt-dlp version " << ytdlpVersion;
        }
        //ffmpeg
        if(Environment::findDependency("ffmpeg").empty())
        {
            builder << "ffmpeg not found" << std::endl;
        }
        else
        {
            std::string ffmpegVersion{ Environment::exec(Environment::findDependency("ffmpeg").string() + " -version") };
            builder << ffmpegVersion.substr(0, ffmpegVersion.find("Copyright")) << std::endl;
        }
        //aria2c
        if(Environment::findDependency("aria2c").empty())
        {
            builder << "aria2c not found" << std::endl;
        }
        else
        {
            std::string aria2cVersion{ Environment::exec(Environment::findDependency("aria2c").string() + " --version") };
            builder << aria2cVersion.substr(0, aria2cVersion.find('\n')) << std::endl;
        }
        //Extra
        if(!extraInformation.empty())
        {
            builder << std::endl << extraInformation << std::endl;
#ifdef __linux__
            builder << Environment::exec("locale");
#endif
        }
        return Environment::getDebugInformation(m_appInfo, builder.str());
    }

    bool MainWindowController::canShutdown() const
    {
        return m_downloadManager.getRemainingDownloadsCount() == 0;
    }

    std::string MainWindowController::getHelpUrl(const std::string& pageName)
    {
        return Documentation::getHelpUrl(m_appInfo.getEnglishShortName(), m_appInfo.getHtmlDocsStore(), pageName);
    }

    bool MainWindowController::canDownload() const
    {
        if(Environment::findDependency("yt-dlp").empty())
        {
            m_logger.log(Logging::LogLevel::Error, "yt-dlp not found.");
        }
        if(Environment::findDependency("ffmpeg").empty())
        {
            m_logger.log(Logging::LogLevel::Error, "ffmpeg not found.");
        }
        if(Environment::findDependency("aria2c").empty())
        {
            m_logger.log(Logging::LogLevel::Error, "aria2c not found.");
        }
        return !Environment::findDependency("yt-dlp").empty() && !Environment::findDependency("ffmpeg").empty() && !Environment::findDependency("aria2c").empty();
    }

    std::shared_ptr<AddDownloadDialogController> MainWindowController::createAddDownloadDialogController()
    {
        return std::make_shared<AddDownloadDialogController>(m_downloadManager, m_dataFileManager, m_keyring);
    }

    std::shared_ptr<CredentialDialogController> MainWindowController::createCredentialDialogController(const DownloadCredentialNeededEventArgs& args)
    {
        return std::make_shared<CredentialDialogController>(args, m_keyring);
    }

    std::shared_ptr<KeyringDialogController> MainWindowController::createKeyringDialogController()
    {
        return std::make_shared<KeyringDialogController>(m_keyring);
    }

    std::shared_ptr<PreferencesViewController> MainWindowController::createPreferencesViewController()
    {
        return std::make_shared<PreferencesViewController>(m_dataFileManager.get<Configuration>("config"), m_dataFileManager.get<DownloadHistory>("history"));
    }

#ifdef _WIN32
    Nickvision::App::WindowGeometry MainWindowController::startup(HWND hwnd)
#elif defined(__linux__)
    Nickvision::App::WindowGeometry MainWindowController::startup(const std::string& desktopFile)
#else
    Nickvision::App::WindowGeometry MainWindowController::startup()
#endif
    {
        if (m_started)
        {
            return m_dataFileManager.get<Configuration>("config").getWindowGeometry();
        }
        //Load taskbar item
#ifdef _WIN32
        if(m_taskbar.connect(hwnd))
        {
            m_logger.log(Logging::LogLevel::Info, "Connected to Windows taskbar.");
        }
        else
        {
            m_logger.log(Logging::LogLevel::Error, "Unable to connect to Windows taskbar.");
        }
        if (m_dataFileManager.get<Configuration>("config").getAutomaticallyCheckForUpdates())
        {
            checkForUpdates();
        }
#elif defined(__linux__)
        if(m_taskbar.connect(desktopFile))
        {
            m_logger.log(Logging::LogLevel::Info, "Connected to Linux taskbar.");
        }
        else
        {
            m_logger.log(Logging::LogLevel::Error, "Unable to connect to Linux taskbar.");
        }
#endif
        //Load DownloadManager
        size_t recoveredDownloads{ m_downloadManager.startup() };
        if(recoveredDownloads > 0)
        {
            m_notificationSent.invoke({ std::vformat(_n("Recovered {} download", "Recovered {} downloads", recoveredDownloads), std::make_format_args(recoveredDownloads)), NotificationSeverity::Informational });
        }
        //Check if disclaimer should be shown
        if(m_dataFileManager.get<Configuration>("config").getShowDisclaimerOnStartup())
        {
            m_disclaimerTriggered.invoke({ _("The authors of Nickvision Parabolic are not responsible/liable for any misuse of this program that may violate local copyright/DMCA laws. Users use this application at their own risk.") });
        }
        m_started = true;
        return m_dataFileManager.get<Configuration>("config").getWindowGeometry();
    }

    void MainWindowController::shutdown(const WindowGeometry& geometry)
    {
        //Save config
        Configuration& config{ m_dataFileManager.get<Configuration>("config") };
        config.setWindowGeometry(geometry);
        config.save();
    }

    void MainWindowController::checkForUpdates()
    {
        if(!m_updater)
        {
            return;
        }
        m_logger.log(Logging::LogLevel::Info, "Checking for updates...");
        std::thread worker{ [this]()
        {
            Version latest{ m_updater->fetchCurrentVersion(VersionType::Stable) };
            if (!latest.empty())
            {
                if (latest > m_appInfo.getVersion())
                {
                    m_logger.log(Logging::LogLevel::Info, "Update found: " + latest.str());
                    m_notificationSent.invoke({ _("New update available"), NotificationSeverity::Success, "update" });
                }
                else
                {
                    m_logger.log(Logging::LogLevel::Info, "No updates found.");
                }
            }
            else
            {
                m_logger.log(Logging::LogLevel::Warning, "Unable to fetch latest app version.");
            }
        } };
        worker.detach();
    }

#ifdef _WIN32
    void MainWindowController::windowsUpdate()
    {
        if(m_updater)
        {
            return;
        }
        m_logger.log(Logging::LogLevel::Info, "Fetching Windows app update...");
        std::thread worker{ [this]()
        {
            if (m_updater->windowsUpdate(VersionType::Stable))
            {
                m_logger.log(Logging::LogLevel::Info, "Windows app update started.");
            }
            else
            {
                m_logger.log(Logging::LogLevel::Error, "Unable to fetch Windows app update.");
                m_notificationSent.invoke({ _("Unable to download and install update"), NotificationSeverity::Error, "error" });
            }
        } };
        worker.detach();
    }
#endif

    void MainWindowController::log(Logging::LogLevel level, const std::string& message, const std::source_location& source)
    {
        m_logger.log(level, message, source);
    }

    void MainWindowController::onConfigurationSaved()
    {
        m_logger.log(Logging::LogLevel::Info, "Configuration saved.");
        if(m_dataFileManager.get<Configuration>("config").getPreventSuspend())
        {
            if(m_suspendInhibitor.inhibit())
            {
                m_logger.log(Logging::LogLevel::Info, "Inhibited system suspend.");
            }
            else
            {
                m_logger.log(Logging::LogLevel::Error, "Unable to inhibit system suspend.");
            }
        }
        else
        {
            if(m_suspendInhibitor.uninhibit())
            {
                m_logger.log(Logging::LogLevel::Info, "Uninhibited system suspend.");
            }
            else
            {
                m_logger.log(Logging::LogLevel::Error, "Unable to uninhibit system suspend.");
            }
        }
        m_downloadManager.setDownloaderOptions(m_dataFileManager.get<Configuration>("config").getDownloaderOptions());
    }
}
