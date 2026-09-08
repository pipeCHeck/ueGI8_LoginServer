#if WITH_DEV_AUTOMATION_TESTS

#include "../DataGameInstanceSubsystem.h"
#include "../Web/WebApiSubsystem.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	UDataGameInstanceSubsystem* CreateWebApiDataSubsystem()
	{
		UGameInstance* GameInstance = NewObject<UGameInstance>();
		return NewObject<UDataGameInstanceSubsystem>(GameInstance);
	}

	TSharedRef<FJsonObject> MakeValidGameServer()
	{
		TSharedRef<FJsonObject> GameServer = MakeShared<FJsonObject>();
		GameServer->SetStringField(TEXT("server_id"), TEXT("123e4567-e89b-12d3-a456-426614174000"));
		GameServer->SetStringField(TEXT("host"), TEXT("192.168.0.25"));
		GameServer->SetNumberField(TEXT("port"), 7777);
		return GameServer;
	}

	TSharedRef<FJsonObject> MakeLoginResponse(const TSharedRef<FJsonObject>& GameServer)
	{
		TSharedRef<FJsonObject> LoginResponse = MakeShared<FJsonObject>();
		LoginResponse->SetObjectField(TEXT("game_server"), GameServer);
		return LoginResponse;
	}

	void SetStaleGameServer(UDataGameInstanceSubsystem& Data)
	{
		Data.SetGameServer(TEXT("192.168.0.10"), 7000, TEXT("stale-server-id"));
	}

	bool TestGameServerIsEmpty(FAutomationTestBase& Test, const FString& CaseName,
		const UDataGameInstanceSubsystem& Data)
	{
		bool bSuccess = true;
		bSuccess &= Test.TestEqual(CaseName + TEXT(": host is empty"), Data.GameServerHost, FString());
		bSuccess &= Test.TestEqual(CaseName + TEXT(": port is zero"), Data.GameServerPort, 0);
		bSuccess &= Test.TestEqual(CaseName + TEXT(": ID is empty"), Data.GameServerId, FString());
		bSuccess &= Test.TestFalse(CaseName + TEXT(": state is invalid"), Data.HasValidGameServer());
		bSuccess &= Test.TestEqual(CaseName + TEXT(": address is empty"), Data.GetGameServerAddress(), FString());
		return bSuccess;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWebApiValidGameServerResponseTest,
	"L20260713_Day03.WebApi.LoginGameServer.ValidObject",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWebApiValidGameServerResponseTest::RunTest(const FString& Parameters)
{
	UDataGameInstanceSubsystem* Data = CreateWebApiDataSubsystem();
	const TSharedRef<FJsonObject> LoginResponse = MakeLoginResponse(MakeValidGameServer());

	const ELoginGameServerParseResult Result = ApplyGameServerFromLoginResponse(LoginResponse, *Data);

	TestTrue(TEXT("A complete game_server object is valid"), Result == ELoginGameServerParseResult::Valid);
	TestEqual(TEXT("Parsed host is stored"), Data->GameServerHost, FString(TEXT("192.168.0.25")));
	TestEqual(TEXT("Parsed port is stored"), Data->GameServerPort, 7777);
	TestEqual(
		TEXT("Parsed server ID is stored"),
		Data->GameServerId,
		FString(TEXT("123e4567-e89b-12d3-a456-426614174000")));
	TestTrue(TEXT("Parsed game server is valid"), Data->HasValidGameServer());
	TestEqual(
		TEXT("Parsed address uses Host:Port"),
		Data->GetGameServerAddress(),
		FString(TEXT("192.168.0.25:7777")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWebApiNullGameServerResponseTest,
	"L20260713_Day03.WebApi.LoginGameServer.NullClearsStaleData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWebApiNullGameServerResponseTest::RunTest(const FString& Parameters)
{
	UDataGameInstanceSubsystem* Data = CreateWebApiDataSubsystem();
	SetStaleGameServer(*Data);
	TSharedRef<FJsonObject> LoginResponse = MakeShared<FJsonObject>();
	LoginResponse->SetField(TEXT("game_server"), MakeShared<FJsonValueNull>());

	const ELoginGameServerParseResult Result = ApplyGameServerFromLoginResponse(LoginResponse, *Data);

	TestTrue(TEXT("A null game_server means no server"), Result == ELoginGameServerParseResult::NotPresent);
	TestGameServerIsEmpty(*this, TEXT("Null game_server"), *Data);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWebApiMissingGameServerResponseTest,
	"L20260713_Day03.WebApi.LoginGameServer.MissingFieldClearsStaleData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWebApiMissingGameServerResponseTest::RunTest(const FString& Parameters)
{
	UDataGameInstanceSubsystem* Data = CreateWebApiDataSubsystem();
	SetStaleGameServer(*Data);
	const TSharedRef<FJsonObject> LoginResponse = MakeShared<FJsonObject>();

	const ELoginGameServerParseResult Result = ApplyGameServerFromLoginResponse(LoginResponse, *Data);

	TestTrue(TEXT("A missing game_server field means no server"), Result == ELoginGameServerParseResult::NotPresent);
	TestGameServerIsEmpty(*this, TEXT("Missing game_server"), *Data);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWebApiInvalidGameServerResponseTest,
	"L20260713_Day03.WebApi.LoginGameServer.InvalidObjectClearsStaleData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWebApiInvalidGameServerResponseTest::RunTest(const FString& Parameters)
{
	UDataGameInstanceSubsystem* Data = CreateWebApiDataSubsystem();

	auto TestInvalidGameServer = [this, Data](const TCHAR* CaseName, const TSharedRef<FJsonObject>& GameServer)
		{
			SetStaleGameServer(*Data);
			const TSharedRef<FJsonObject> LoginResponse = MakeLoginResponse(GameServer);

			const ELoginGameServerParseResult Result = ApplyGameServerFromLoginResponse(LoginResponse, *Data);

			TestTrue(
				FString(CaseName) + TEXT(": parse result is invalid"),
				Result == ELoginGameServerParseResult::Invalid);
			TestGameServerIsEmpty(*this, CaseName, *Data);
		};

	TSharedRef<FJsonObject> MissingHost = MakeValidGameServer();
	MissingHost->RemoveField(TEXT("host"));
	TestInvalidGameServer(TEXT("Missing host"), MissingHost);

	TSharedRef<FJsonObject> EmptyHost = MakeValidGameServer();
	EmptyHost->SetStringField(TEXT("host"), TEXT(""));
	TestInvalidGameServer(TEXT("Empty host"), EmptyHost);

	TSharedRef<FJsonObject> MissingPort = MakeValidGameServer();
	MissingPort->RemoveField(TEXT("port"));
	TestInvalidGameServer(TEXT("Missing port"), MissingPort);

	TSharedRef<FJsonObject> ZeroPort = MakeValidGameServer();
	ZeroPort->SetNumberField(TEXT("port"), 0);
	TestInvalidGameServer(TEXT("Zero port"), ZeroPort);

	TSharedRef<FJsonObject> PortAboveMaximum = MakeValidGameServer();
	PortAboveMaximum->SetNumberField(TEXT("port"), 65536);
	TestInvalidGameServer(TEXT("Port above 65535"), PortAboveMaximum);

	TSharedRef<FJsonObject> MissingServerId = MakeValidGameServer();
	MissingServerId->RemoveField(TEXT("server_id"));
	TestInvalidGameServer(TEXT("Missing server ID"), MissingServerId);

	TSharedRef<FJsonObject> EmptyServerId = MakeValidGameServer();
	EmptyServerId->SetStringField(TEXT("server_id"), TEXT(""));
	TestInvalidGameServer(TEXT("Empty server ID"), EmptyServerId);

	return true;
}

#endif
