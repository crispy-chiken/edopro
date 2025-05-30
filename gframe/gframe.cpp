#include "config.h"
#include <event2/thread.h>
#include <IrrlichtDevice.h>
#include <IGUIButton.h>
#include <IGUICheckBox.h>
#include <IGUIEditBox.h>
#include <IGUIWindow.h>
#include <IGUIEnvironment.h>
#include <ISceneManager.h>
#include "client_updater.h"
#include "cli_args.h"
#include "config.h"
#include "data_handler.h"
#include "logging.h"
#include "game.h"
#include "log.h"
#include "joystick_wrapper.h"
#include "utils_gui.h"
#include "fmt.h"
#include "curl.h"
#if EDOPRO_MACOS
#include "osx_menu.h"
#endif

bool is_from_discord = false;
bool open_file = false;
epro::path_string open_file_name = EPRO_TEXT("");
bool show_changelog = false;
ygo::Game* ygo::mainGame = nullptr;
ygo::ImageDownloader* ygo::gImageDownloader = nullptr;
ygo::DataManager* ygo::gDataManager = nullptr;
ygo::SoundManager* ygo::gSoundManager = nullptr;
ygo::GameConfig* ygo::gGameConfig = nullptr;
ygo::RepoManager* ygo::gRepoManager = nullptr;
ygo::DeckManager* ygo::gdeckManager = nullptr;
ygo::ClientUpdater* ygo::gClientUpdater = nullptr;
JWrapper* gJWrapper = nullptr;

namespace {
inline void TriggerEvent(irr::gui::IGUIElement* target, irr::gui::EGUI_EVENT_TYPE type) {
	irr::SEvent event;
	event.EventType = irr::EET_GUI_EVENT;
	event.GUIEvent.EventType = type;
	event.GUIEvent.Caller = target;
	ygo::mainGame->device->postEventFromUser(event);
}

inline void SetCheckbox(irr::gui::IGUICheckBox* chk, bool state) {
	chk->setChecked(state);
	TriggerEvent(chk, irr::gui::EGET_CHECKBOX_CHANGED);
}

enum LAUNCH_PARAM {
	WORK_DIR,
	MUTE,
	CHANGELOG,
	DISCORD,
	OVERRIDE_UPDATE_URL,
	WANTS_TO_RUN_AS_ADMIN,
	COUNT,
};

LAUNCH_PARAM GetOption(epro::path_stringview option) {
	if(option.size() == 1) {
		switch(option.front()) {
		case EPRO_TEXT('C'): return LAUNCH_PARAM::WORK_DIR;
		case EPRO_TEXT('m'): return LAUNCH_PARAM::MUTE;
		case EPRO_TEXT('l'): return LAUNCH_PARAM::CHANGELOG;
		case EPRO_TEXT('D'): return LAUNCH_PARAM::DISCORD;
		case EPRO_TEXT('u'): return LAUNCH_PARAM::OVERRIDE_UPDATE_URL;
		default: return LAUNCH_PARAM::COUNT;
		}
	}
	if(option == EPRO_TEXT("i-want-to-be-admin"_sv))
		return LAUNCH_PARAM::WANTS_TO_RUN_AS_ADMIN;
	return LAUNCH_PARAM::COUNT;
}

struct Option {
	bool enabled{ false };
	epro::path_stringview argument;
};

using args_t = std::array<Option, LAUNCH_PARAM::COUNT>;

#define PARAM_CHECK(x) (parameter[1] == EPRO_TEXT(x))
#define RUN_IF(x,expr) (PARAM_CHECK(x)) {i++; if(i < argc) {expr;} continue;}
#define SET_TXT(elem) ygo::mainGame->elem->setText(ygo::Utils::ToUnicodeIfNeeded(parameter).data())

args_t ParseArguments(int argc, epro::path_char* argv[]) {
	args_t res;
	for(int i = 1; i < argc; ++i) {
		epro::path_stringview parameter = argv[i];
		if(parameter.size() < 2)
			break;
		if(parameter[0] == EPRO_TEXT('-')) {
			auto launch_param = GetOption(parameter.substr(1));
			if(launch_param == LAUNCH_PARAM::COUNT)
				continue;
			epro::path_stringview argument;
			if(i + 1 < argc) {
				const auto* next = argv[i + 1];
				if(next[0] != EPRO_TEXT('-')) {
					argument = next;
					i++;
				}
				else
				{
					if RUN_IF('p', SET_TXT(ebJoinPort))
					{
					}
				}
			}
			res[launch_param].enabled = true;
			res[launch_param].argument = argument;
			continue;
		}
	}
	return res;
}

void CheckArguments(const args_t& args) {
	if(args[LAUNCH_PARAM::MUTE].enabled) {
		ygo::GUIUtils::SetCheckbox(ygo::mainGame->device, ygo::mainGame->tabSettings.chkEnableSound, false);
		ygo::GUIUtils::SetCheckbox(ygo::mainGame->device, ygo::mainGame->tabSettings.chkEnableMusic, false);
	}

	ClickButton(ygo::mainGame->btnLanMode);
}

inline void ThreadsStartup() {
#if EDOPRO_WINDOWS
	const WORD wVersionRequested = MAKEWORD(2, 2);
	WSADATA wsaData;
	auto wsaret = WSAStartup(wVersionRequested, &wsaData);
	if(wsaret != 0)
		throw std::runtime_error(epro::format("Failed to initialize WinSock ({})!", wsaret));
	if(evthread_use_windows_threads() < 0)
		throw std::runtime_error("Failed initialize libevent!");
#else
	if(evthread_use_pthreads() < 0)
		throw std::runtime_error("Failed initialize libevent!");
#endif
	auto res = curl_global_init(CURL_GLOBAL_SSL);
	if(res != CURLE_OK)
		throw std::runtime_error(epro::format("Curl error: ({}) {}", res, curl_easy_strerror(res)));
}
inline void ThreadsCleanup() {
	curl_global_cleanup();
	libevent_global_shutdown();
#if EDOPRO_WINDOWS
	WSACleanup();
#endif
}

//clang below version 11 (llvm version 8) has a bug with brace class initialization
//where it can't properly deduce the destructors of its members
//https://reviews.llvm.org/D45898
//https://bugs.llvm.org/show_bug.cgi?id=28280
//add a workaround to construct the game object indirectly
//to avoid constructing it with brace initialization
#if defined(__clang_major__) && __clang_major__ <= 10
class Game {
	ygo::Game game;
public:
	Game() :game() {};
	ygo::Game* operator&() { return &game; }
};
#else
using Game = ygo::Game;
#endif

}

#if EDOPRO_WINDOWS
#define ADMIN_STR "administrator"
#else
#define ADMIN_STR "root"
#endif

int edopro_main(const args_t& args) {
	std::puts(EDOPRO_VERSION_STRING_DEBUG);
	if(ygo::Utils::IsRunningAsAdmin() && !args[LAUNCH_PARAM::WANTS_TO_RUN_AS_ADMIN].enabled) {
		constexpr auto err = "Attempted to run the game as " ADMIN_STR ".\n"
			"You should NEVER have to run the game with elevated priviledges.\n"
			"If for some reason you REALLY want to do that, launch the game with the option \"-i-want-to-be-admin\""sv;
		epro::print("{}\n", err);
		ygo::GUIUtils::ShowErrorWindow("Initialization fail", err);
		return EXIT_FAILURE;
	}
	{
		const auto& workdir = args[LAUNCH_PARAM::WORK_DIR];
		const epro::path_stringview dest = workdir.enabled ? workdir.argument : ygo::Utils::GetExeFolder();
		if(!ygo::Utils::SetWorkingDirectory(dest)) {
			const auto err = epro::format("failed to change directory to: {} ({})",
										 ygo::Utils::ToUTF8IfNeeded(dest), ygo::Utils::GetLastErrorString());
			ygo::ErrorLog(err);
			epro::print("{}\n", err);
			ygo::GUIUtils::ShowErrorWindow("Initialization fail", err);
			return EXIT_FAILURE;
		}
	}
	ygo::Utils::SetupCrashDumpLogging();
	try {
		ThreadsStartup();
		std::atexit(ThreadsCleanup);
	} catch(const std::exception& e) {
		epro::stringview text(e.what());
		ygo::ErrorLog(text);
		epro::print("{}\n", text);
		ygo::GUIUtils::ShowErrorWindow("Initialization fail", text);
		return EXIT_FAILURE;
	}
	show_changelog = args[LAUNCH_PARAM::CHANGELOG].enabled;
	ygo::ClientUpdater updater(args[LAUNCH_PARAM::OVERRIDE_UPDATE_URL].argument);
	ygo::gClientUpdater = &updater;
	std::unique_ptr<ygo::DataHandler> data{ nullptr };
	try {
		data = std::make_unique<ygo::DataHandler>();
		ygo::gImageDownloader = data->imageDownloader.get();
		ygo::gDataManager = data->dataManager.get();
		ygo::gSoundManager = data->sounds.get();
		ygo::gGameConfig = data->configs.get();
		ygo::gRepoManager = data->gitManager.get();
		ygo::gdeckManager = data->deckManager.get();
	}
	catch(const std::exception& e) {
		epro::stringview text(e.what());
		ygo::ErrorLog(text);
		epro::print("{}\n", text);
		ygo::GUIUtils::ShowErrorWindow("Initialization fail", text);
		return EXIT_FAILURE;
	}
	if (!data->configs->noClientUpdates)
		updater.CheckUpdates();
#if EDOPRO_WINDOWS
	if(!data->configs->showConsole) {
		FILE* fDummy;
		freopen_s(&fDummy, "NUL", "r", stdin);
		freopen_s(&fDummy, "NUL", "w", stderr);
		freopen_s(&fDummy, "NUL", "w", stdout);
		FreeConsole();
	}
#endif
#if EDOPRO_MACOS
	EDOPRO_SetupMenuBar([]() {
		ygo::gGameConfig->fullscreen = !ygo::gGameConfig->fullscreen;
		ygo::mainGame->gSettings.chkFullscreen->setChecked(ygo::gGameConfig->fullscreen);
	});
#endif
	srand(static_cast<uint32_t>(time(nullptr)));
	std::unique_ptr<JWrapper> joystick{ nullptr };
	bool firstlaunch = true;
	bool reset = false;
	do {
		Game _game{};
		ygo::mainGame = &_game;
		std::swap(data->tmp_device, ygo::mainGame->device);
		try {
			ygo::mainGame->Initialize();
		}
		catch(const std::exception& e) {
			epro::stringview text(e.what());
			ygo::ErrorLog(text);
			epro::print("{}\n", text);
			ygo::GUIUtils::ShowErrorWindow("Assets load fail", text);
			return EXIT_FAILURE;
		}
		if(firstlaunch) {
			joystick = std::make_unique<JWrapper>(ygo::mainGame->device.get());
			gJWrapper = joystick.get();
			firstlaunch = false;
			CheckArguments(args);
		}
		reset = ygo::mainGame->MainLoop();
		std::swap(data->tmp_device, ygo::mainGame->device);
		if(reset) {
			auto device = data->tmp_device;
			device->setEventReceiver(nullptr);
			auto driver = device->getVideoDriver();
			/*the gles drivers have an additional cache, that isn't cleared when the textures are removed,
			since it's not a big deal clearing them, as they'll be reused, they aren't cleared*/
			/*driver->removeAllTextures();*/
			driver->removeAllHardwareBuffers();
			driver->removeAllOcclusionQueries();
			device->getSceneManager()->clear();
			auto env = device->getGUIEnvironment();
			env->clear();
		}
	} while(reset);
	return EXIT_SUCCESS;
}
