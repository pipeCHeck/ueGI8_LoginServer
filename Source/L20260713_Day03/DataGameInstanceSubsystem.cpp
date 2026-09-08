// Fill out your copyright notice in the Description page of Project Settings.


#include "DataGameInstanceSubsystem.h"

void UDataGameInstanceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
}

void UDataGameInstanceSubsystem::Deinitialize()
{
}

void UDataGameInstanceSubsystem::ClearGameServer()
{
	GameServerHost.Empty();
	GameServerPort = 0;
	GameServerId.Empty();
}

void UDataGameInstanceSubsystem::SetGameServer(
	const FString& InHost,
	const int32 InPort,
	const FString& InServerId)
{
	if (InHost.TrimStartAndEnd().IsEmpty()
		|| InPort < 1
		|| InPort > 65535
		|| InServerId.TrimStartAndEnd().IsEmpty())
	{
		ClearGameServer();
		return;
	}

	GameServerHost = InHost;
	GameServerPort = InPort;
	GameServerId = InServerId;
}

bool UDataGameInstanceSubsystem::HasValidGameServer() const
{
	return !GameServerHost.TrimStartAndEnd().IsEmpty()
		&& GameServerPort >= 1
		&& GameServerPort <= 65535
		&& !GameServerId.TrimStartAndEnd().IsEmpty();
}

FString UDataGameInstanceSubsystem::GetGameServerAddress() const
{
	return HasValidGameServer()
		? FString::Printf(TEXT("%s:%d"), *GameServerHost, GameServerPort)
		: FString();
}
