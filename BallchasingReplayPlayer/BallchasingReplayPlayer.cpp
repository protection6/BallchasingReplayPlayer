#include "pch.h"
#include "BallchasingReplayPlayer.h"
#include "utils/parser.h"

#include "SettingsManager.h"

#include <fstream>


BAKKESMOD_PLUGIN(BallchasingReplayPlayer, "BallchasingProtocolHandler", plugin_version, PLUGINTYPE_FREEPLAY)

std::shared_ptr<CVarManagerWrapper> _globalCvarManager;

void BallchasingReplayPlayer::onLoad()
{
	_globalCvarManager = cvarManager;
	RegisterURIHandler();
	pipe_server_thread_ = std::thread{&BallchasingReplayPlayer::StartPipeServer, this};
}

void BallchasingReplayPlayer::onUnload()
{
	pipe_server_running = false;
	CancelSynchronousIo(pipe_server_thread_.native_handle());
	if (pipe_server_thread_.joinable())
	{
		//LOG("Joining pipe server thread");
		pipe_server_thread_.join();
	}
}

void BallchasingReplayPlayer::RegisterURIHandler()
{
	RegisterySettingsManager settings;

	settings.SaveSetting(L"", L"URL:ballchasing protocol", L"Software\\Classes\\ballchasing", HKEY_CURRENT_USER);
	settings.SaveSetting(L"URL Protocol", L"ballchasing", L"Software\\Classes\\ballchasing", HKEY_CURRENT_USER);
	const auto* const command = L"cmd /c echo  %1 > \\\\.\\pipe\\ballchasing";
	settings.SaveSetting(L"", command, L"Software\\Classes\\ballchasing\\shell\\open\\command", HKEY_CURRENT_USER);
}

void BallchasingReplayPlayer::ProcessPipeMessage(std::string replay_id)
{
	std::vector<std::string> splitted;
	split(replay_id, splitted, '/');
	replay_id = splitted[splitted.size() - 1];
	replay_id = trim(replay_id);
	const auto download_url = fmt::format("https://ballchasing.com/dl/replay/{}", replay_id);
	HttpWrapper::SendRequest(download_url, "POST", {}, [this, replay_id](HttpResponseWrapper res)
	{
		if (!res)
		{
			LOG("Invalid response");
			return;
		}
		if (res.GetResponseCode() != 200)
		{
			LOG("wrong status code for replay download: {}", res.GetResponseCode());
			//LOG("{}", res.GetContentAsString());
			return;
		}
		auto [data, size] = res.GetContent();
		//LOG("Request content size: {}", size);
		const auto save_path = gameWrapper->GetDataFolder() / "ballchasing" / "dl";
		const auto file_path = save_path / (replay_id + ".replay");
		create_directories(save_path);
		auto out = std::ofstream(file_path, std::ios_base::binary);
		if (out)
		{
			out.write(data, size);
			out.close();
		}
		gameWrapper->PlayReplay(file_path.wstring());
	});
}

void BallchasingReplayPlayer::StartPipeServer()
{
	// This is 99% copy paste from some stackoverflow post
	char buffer[1024];
	DWORD dwRead;


	HANDLE hPipe = CreateNamedPipe(TEXT("\\\\.\\pipe\\ballchasing"),
		PIPE_ACCESS_INBOUND,
		PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
		// FILE_FLAG_FIRST_PIPE_INSTANCE is not needed but forces CreateNamedPipe(..) to fail if the pipe already exists...
		PIPE_UNLIMITED_INSTANCES,
		1024 * 16,
		1024 * 16,
		NMPWAIT_USE_DEFAULT_WAIT,
		NULL);

	if (hPipe == INVALID_HANDLE_VALUE)
	{
		int errorno = GetLastError();
		LOG("Error creating pipe {}", errorno);
		return;
	}

	while (hPipe != INVALID_HANDLE_VALUE)
	{
		if (!pipe_server_running)
		{
			//LOG("Stopping server");
			break;
		}
		//LOG("Checking pipe");
		if (ConnectNamedPipe(hPipe, NULL) != FALSE) // wait for someone to connect to the pipe
		{
			while (ReadFile(hPipe, buffer, sizeof(buffer) - 1, &dwRead, NULL) != FALSE)
			{
				/* add terminating zero */
				buffer[dwRead] = '\0';
				gameWrapper->Execute([this, msg = std::string(buffer)](...) { ProcessPipeMessage(msg); });
			}
		}
		DisconnectNamedPipe(hPipe);
	}
	pipe_server_running = false;
	//LOG("Pipe server stopping");
}
