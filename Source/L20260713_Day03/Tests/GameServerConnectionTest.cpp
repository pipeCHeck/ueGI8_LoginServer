#if WITH_DEV_AUTOMATION_TESTS

#include "../DataGameInstanceSubsystem.h"
#include "../Title/TitleWidgetBase.h"
#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	UDataGameInstanceSubsystem* CreateConnectionDataSubsystem()
	{
		UGameInstance* GameInstance = NewObject<UGameInstance>();
		return NewObject<UDataGameInstanceSubsystem>(GameInstance);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameServerConnectionRequiresLoginTest,
	"L20260713_Day03.Title.GameServerConnection.RequiresLogin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameServerConnectionRequiresLoginTest::RunTest(const FString& Parameters)
{
	UDataGameInstanceSubsystem* Data = CreateConnectionDataSubsystem();
	Data->SetGameServer(TEXT("192.168.0.25"), 7777, TEXT("valid-id"));

	const FString Address = ResolveGameServerConnectionAddress(false, *Data);

	TestEqual(TEXT("A logged-out client cannot resolve a game server address"), Address, FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameServerConnectionRequiresServerTest,
	"L20260713_Day03.Title.GameServerConnection.RequiresServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameServerConnectionRequiresServerTest::RunTest(const FString& Parameters)
{
	const UDataGameInstanceSubsystem* Data = CreateConnectionDataSubsystem();

	const FString Address = ResolveGameServerConnectionAddress(true, *Data);

	TestEqual(TEXT("A login without a discovered server has no connection address"), Address, FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameServerConnectionUsesDiscoveredServerTest,
	"L20260713_Day03.Title.GameServerConnection.UsesDiscoveredServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameServerConnectionUsesDiscoveredServerTest::RunTest(const FString& Parameters)
{
	UDataGameInstanceSubsystem* Data = CreateConnectionDataSubsystem();
	Data->SetGameServer(TEXT("192.168.0.25"), 7777, TEXT("valid-id"));

	const FString Address = ResolveGameServerConnectionAddress(true, *Data);

	TestEqual(TEXT("A valid discovered server resolves as Host:Port"), Address, FString(TEXT("192.168.0.25:7777")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameServerConnectionSeparatesRegistryAddressTest,
	"L20260713_Day03.Title.GameServerConnection.SeparatesRegistryAddress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameServerConnectionSeparatesRegistryAddressTest::RunTest(const FString& Parameters)
{
	UDataGameInstanceSubsystem* Data = CreateConnectionDataSubsystem();
	Data->ServerIP = TEXT("192.168.0.10");
	Data->SetGameServer(TEXT("192.168.0.25"), 7777, TEXT("valid-id"));

	const FString Address = ResolveGameServerConnectionAddress(true, *Data);

	TestEqual(TEXT("Connection uses the game server address"), Address, FString(TEXT("192.168.0.25:7777")));
	TestNotEqual(TEXT("Connection does not use the FastAPI address"), Address, Data->ServerIP);
	return true;
}

#endif
