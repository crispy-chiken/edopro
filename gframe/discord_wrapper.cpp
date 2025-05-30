#ifdef DISCORD_APP_ID
#include <chrono>
#include <nlohmann/json.hpp>
#include <IGUIElement.h>
#include <IrrlichtDevice.h>
#include <IGUIButton.h>
#include <IGUIEditBox.h>
#include <IGUIStaticText.h>
#include <IGUITabControl.h>
#include <IGUIWindow.h>
#include "discord_register.h"
#include "discord_rpc.h"
#include "game.h"
#include "duelclient.h"
#include "logging.h"
#include "config.h"
#include "server_lobby.h"
#include "fmt.h"
#endif
#include "text_types.h"
#include "discord_wrapper.h"

#ifdef DISCORD_APP_ID
#if EDOPRO_WINDOWS
#define formatstr EPRO_TEXT("\"{0}\" -C \"{1}\" -D")
//The registry entry on windows seems to need the path with \ as separator rather than /
epro::path_string Unescape(epro::path_string path) {
	std::replace(path.begin(), path.end(), EPRO_TEXT('/'), EPRO_TEXT('\\'));
	return path;
}
#elif EDOPRO_LINUX
#define formatstr R"(bash -c "\\"{0}\\" -C \\"{1}\\" -D")"
#define Unescape(x) x
#endif
#endif //DISCORD_APP_ID

bool DiscordWrapper::Initialize() {
#ifdef DISCORD_APP_ID
#if EDOPRO_WINDOWS || EDOPRO_LINUX
	epro::path_string param = epro::format(formatstr, Unescape(ygo::Utils::GetExePath()), ygo::Utils::GetWorkingDirectory());
	Discord_Register(DISCORD_APP_ID, ygo::Utils::ToUTF8IfNeeded(param).data());
#elif EDOPRO_MACOS
	RegisterURL(DISCORD_APP_ID);
#endif //EDOPRO_WINDOWS
	return (initialized = true);
#else
	return false;
#endif //DISCORD_APP_ID
}

void DiscordWrapper::UpdatePresence([[maybe_unused]] PresenceType type) {
#ifdef DISCORD_APP_ID
	auto CreateSecret = [&secret_buf=secret_buf](bool update) {
		if(update) {
			auto& secret = ygo::mainGame->dInfo.secret;
			//Since with ipv6 addresses we need as much space as possible in the secret string as it's only 128 bytes
			//use single letter json fields:
			// i->id
			// a->address
			// p->port
			// s->password
			auto ret = epro::format_to_n(secret_buf, sizeof(secret_buf) - 1, R"({{"i":{},"a":"{}","p":{},"s":"{}"}})",
							 secret.game_id, secret.host.address, secret.host.port, BufferIO::EncodeUTF8(secret.pass));
			*ret.out = '\0';
		}
		return secret_buf;
	};
	if(type == INITIALIZE && !running) {
		Connect();
		running = true;
		return;
	}
	if((type == TERMINATE || type == DISCONNECT) && running) {
		if(type == TERMINATE) {
			DiscordEventHandlers handlers{};
			Discord_UpdateHandlers(&handlers);
		}
		Disconnect();
		running = false;
		return;
	}
	if(!running)
		return;
	if(presence == type)
		return;
	presence = type;
	if(type == CLEAR) {
		Discord_ClearPresence();
		return;
	}
	DiscordRichPresence discordPresence{};
	std::string presenceState;
	std::string partyid;
	switch(presence) {
		case MENU: {
			discordPresence.details = "In menu";
			break;
		}
		case IN_LOBBY:
		case DUEL:
		case DUEL_STARTED:
		case DECK_SIDING: {
			auto count = ygo::DuelClient::GetPlayersCount();
			discordPresence.partySize = std::max(1u, count.first + count.second);
			discordPresence.partyMax = ygo::mainGame->dInfo.team1 + ygo::mainGame->dInfo.team2 + ygo::DuelClient::GetSpectatorsCount() + 1;
			if(presence == IN_LOBBY) {
				discordPresence.details = "Hosting a Duel";
			} else {
				if(presence == DECK_SIDING)
					discordPresence.details = "Side decking";
				else
					discordPresence.details = "Dueling";
			}
			if(((ygo::mainGame->dInfo.team1 + ygo::mainGame->dInfo.team2) > 2) || ygo::mainGame->dInfo.isRelay)
				presenceState = epro::format("{}: {} vs {}", ygo::mainGame->dInfo.isRelay ? "Relay" : "Tag", ygo::mainGame->dInfo.team1, ygo::mainGame->dInfo.team2).data();
			else
				presenceState = "1 vs 1";
			if(ygo::mainGame->dInfo.best_of) {
				presenceState += epro::format(" (best of {})", ygo::mainGame->dInfo.best_of);
			}
			if(ygo::mainGame->dInfo.secret.game_id) {
				partyid = epro::format("{}{}", ygo::mainGame->dInfo.secret.game_id, ygo::mainGame->dInfo.secret.host.address);
				discordPresence.joinSecret = CreateSecret(previous_gameid != ygo::mainGame->dInfo.secret.game_id);
				previous_gameid = ygo::mainGame->dInfo.secret.game_id;
			}
			break;
		}
		case REPLAY: {
			discordPresence.details = "Watching a replay";
			break;
		}
		case PUZZLE: {
			discordPresence.details = "Playing a puzzle";
			break;
		}
		case HAND_TEST: {
			discordPresence.details = "Testing hands";
			break;
		}
		case DECK: {
			discordPresence.details = "Editing a deck";
			break;
		}
		case CLEAR:
			break;
		default:
			break;
	}
	discordPresence.state = presenceState.data();
	discordPresence.startTimestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	discordPresence.largeImageKey = "game-icon";
	discordPresence.partyId = partyid.data();
	Discord_UpdatePresence(&discordPresence);
#endif
}

void DiscordWrapper::Check() {
#ifdef DISCORD_APP_ID
	Discord_RunCallbacks();
#endif
}

void DiscordWrapper::Disconnect() {
#ifdef DISCORD_APP_ID
	Discord_ClearPresence();
	Discord_Shutdown();
#endif
}

#ifdef DISCORD_APP_ID
struct DiscordCallbacks {
	static void OnReady(const DiscordUser* connectedUser, void* payload) {
		epro::print("Discord: Connected to user {}#{} - {}\n",
				   connectedUser->username,
				   connectedUser->discriminator,
				   connectedUser->userId);
		static_cast<ygo::Game*>(payload)->discord.connected = true;
	}

	static void OnDisconnected(int errcode, const char* message, void* payload) {
		epro::print("Discord: Disconnected, error code: {} - {}\n", errcode, message);
		static_cast<ygo::Game*>(payload)->discord.connected = false;
	}

	static void OnError(int errcode, const char* message, void* payload) {
	}

	static void OnJoin(const char* secret_str, void* payload) {
		epro::print("Join: {}\n", secret_str);
		auto game = static_cast<ygo::Game*>(payload);
		if((game->is_building && game->is_siding) || game->dInfo.isInDuel || game->dInfo.isInLobby || game->dInfo.isReplay || game->wHostPrepare->isVisible())
			return;
		auto& secret = ygo::mainGame->dInfo.secret;
		try {
			nlohmann::json json = nlohmann::json::parse(secret_str);
			const auto it = json.find("i");
			if(it != json.end()) {
				secret.game_id = it->get<uint32_t>();
				secret.host = epro::Host::resolve(json["a"].get_ref<const std::string&>().data(), json["p"].get<uint16_t>());
				secret.pass = BufferIO::DecodeUTF8(json["s"].get_ref<const std::string&>());
			} else {
				secret.game_id = json["id"].get<uint32_t>();
				secret.host.port = json["port"].get<uint16_t>();
				auto ip = json["addr"].get<uint32_t>();
				secret.host.address.setIP4(&ip);
				secret.pass = BufferIO::DecodeUTF8(json["pass"].get_ref<const std::string&>());
			}
		} catch(const std::exception& e) {
			ygo::ErrorLog("Exception occurred: {}", e.what());
			return;
		}
		if(!ygo::ServerLobby::IsKnownHost(secret.host)) {
			game->needs_to_acknowledge_discord_host = true;
			return;
		}
		ygo::DuelClient::JoinFromDiscord();
	}

	static void OnSpectate(const char* secret, void* payload) {
		epro::print("Join Spectating: {}\n", secret);
	}

	static void OnJoinRequest(const DiscordUser* request, void* payload) {
		epro::print("Discord: Join Request from user {}#{} - {}\n",
				   request->username,
				   request->discriminator,
				   request->userId);
		if(ygo::mainGame->dInfo.secret.pass.empty())
			Discord_Respond(request->userId, DISCORD_REPLY_YES);
		else
			Discord_Respond(request->userId, DISCORD_REPLY_NO);
	}
};
#endif

void DiscordWrapper::Connect() {
#ifdef DISCORD_APP_ID
	DiscordEventHandlers handlers{};
	handlers.ready = DiscordCallbacks::OnReady;
	handlers.disconnected = DiscordCallbacks::OnDisconnected;
	handlers.errored = DiscordCallbacks::OnError;
	handlers.joinGame = DiscordCallbacks::OnJoin;
	handlers.spectateGame = DiscordCallbacks::OnSpectate;
	handlers.joinRequest = DiscordCallbacks::OnJoinRequest;
	handlers.payload = ygo::mainGame;
	Discord_Initialize(DISCORD_APP_ID, &handlers, 0, nullptr);
#endif
}
