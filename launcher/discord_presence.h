#pragma once
#include <string>
namespace DiscordPresence { void Initialize(const std::wstring& applicationId); void SetBrowsing(); void SetJoining(const std::wstring& serverName, const std::wstring& endpoint); void Shutdown(); }
