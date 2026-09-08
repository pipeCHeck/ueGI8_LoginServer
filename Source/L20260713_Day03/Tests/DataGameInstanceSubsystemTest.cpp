#if WITH_DEV_AUTOMATION_TESTS

#include "../DataGameInstanceSubsystem.h"
#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	UDataGameInstanceSubsystem* CreateDataSubsystem()
	{
		UGameInstance* GameInstance = NewObject<UGameInstance>();
		return NewObject<UDataGameInstanceSubsystem>(GameInstance);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDataGameInstanceSubsystemInitialStateTest,
	"L20260713_Day03.DataGameInstanceSubsystem.InitialState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDataGameInstanceSubsystemInitialStateTest::RunTest(const FString& Parameters)
{
	const UDataGameInstanceSubsystem* Data = CreateDataSubsystem();

	TestTrue(TEXT("A data subsystem object is created"), IsValid(Data));
	TestEqual(TEXT("Game server host starts empty"), Data->GameServerHost, FString());
	TestEqual(TEXT("Game server port starts at zero"), Data->GameServerPort, 0);
	TestEqual(TEXT("Game server ID starts empty"), Data->GameServerId, FString());
	TestFalse(TEXT("Initial game server state is invalid"), Data->HasValidGameServer());
	TestEqual(TEXT("Initial game server address is empty"), Data->GetGameServerAddress(), FString());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDataGameInstanceSubsystemSetValidServerTest,
	"L20260713_Day03.DataGameInstanceSubsystem.SetValidServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDataGameInstanceSubsystemSetValidServerTest::RunTest(const FString& Parameters)
{
	UDataGameInstanceSubsystem* Data = CreateDataSubsystem();

	Data->SetGameServer(TEXT("192.168.0.25"), 7777, TEXT("valid-server-id"));

	TestEqual(TEXT("SetGameServer stores the host"), Data->GameServerHost, FString(TEXT("192.168.0.25")));
	TestEqual(TEXT("SetGameServer stores the port"), Data->GameServerPort, 7777);
	TestEqual(TEXT("SetGameServer stores the ID"), Data->GameServerId, FString(TEXT("valid-server-id")));
	TestTrue(TEXT("Complete game server data is valid"), Data->HasValidGameServer());
	TestEqual(
		TEXT("A valid game server address is formatted as Host:Port"),
		Data->GetGameServerAddress(),
		FString(TEXT("192.168.0.25:7777")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDataGameInstanceSubsystemClearServerTest,
	"L20260713_Day03.DataGameInstanceSubsystem.ClearServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDataGameInstanceSubsystemClearServerTest::RunTest(const FString& Parameters)
{
	UDataGameInstanceSubsystem* Data = CreateDataSubsystem();
	Data->SetGameServer(TEXT("192.168.0.25"), 7777, TEXT("valid-server-id"));

	Data->ClearGameServer();

	TestEqual(TEXT("ClearGameServer clears the host"), Data->GameServerHost, FString());
	TestEqual(TEXT("ClearGameServer resets the port"), Data->GameServerPort, 0);
	TestEqual(TEXT("ClearGameServer clears the ID"), Data->GameServerId, FString());
	TestFalse(TEXT("Cleared game server state is invalid"), Data->HasValidGameServer());
	TestEqual(TEXT("Cleared game server address is empty"), Data->GetGameServerAddress(), FString());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDataGameInstanceSubsystemInvalidServerTest,
	"L20260713_Day03.DataGameInstanceSubsystem.InvalidServerClearsState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDataGameInstanceSubsystemInvalidServerTest::RunTest(const FString& Parameters)
{
	UDataGameInstanceSubsystem* Data = CreateDataSubsystem();

	auto TestInvalidInputClearsState = [this, Data](
		const TCHAR* CaseName,
		const FString& Host,
		const int32 Port,
		const FString& ServerId)
		{
			Data->SetGameServer(TEXT("192.168.0.25"), 7777, TEXT("previous-server-id"));
			Data->SetGameServer(Host, Port, ServerId);

			const FString Prefix(CaseName);
			TestEqual(*(Prefix + TEXT(": host is cleared")), Data->GameServerHost, FString());
			TestEqual(*(Prefix + TEXT(": port is reset")), Data->GameServerPort, 0);
			TestEqual(*(Prefix + TEXT(": ID is cleared")), Data->GameServerId, FString());
			TestFalse(*(Prefix + TEXT(": state is invalid")), Data->HasValidGameServer());
			TestEqual(*(Prefix + TEXT(": address is empty")), Data->GetGameServerAddress(), FString());
		};

	TestInvalidInputClearsState(TEXT("Empty host"), TEXT(""), 7777, TEXT("server-id"));
	TestInvalidInputClearsState(TEXT("Whitespace-only host"), TEXT("   "), 7777, TEXT("server-id"));
	TestInvalidInputClearsState(TEXT("Zero port"), TEXT("192.168.0.25"), 0, TEXT("server-id"));
	TestInvalidInputClearsState(TEXT("Port above 65535"), TEXT("192.168.0.25"), 65536, TEXT("server-id"));
	TestInvalidInputClearsState(TEXT("Empty server ID"), TEXT("192.168.0.25"), 7777, TEXT(""));
	TestInvalidInputClearsState(TEXT("Whitespace-only server ID"), TEXT("192.168.0.25"), 7777, TEXT("   "));

	return true;
}

#endif
