#if WITH_DEV_AUTOMATION_TESTS

#include "../Web/WebApiSubsystem.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"

namespace
{
	constexpr TCHAR ExpectedServerId[] = TEXT("123e4567-e89b-12d3-a456-426614174000");
	constexpr TCHAR ExpectedHost[] = TEXT("192.168.0.25");
	constexpr int32 ExpectedPort = 7777;

	TSharedRef<FJsonObject> MakeValidRegistrationResponse()
	{
		TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();
		Response->SetBoolField(TEXT("result"), true);
		Response->SetStringField(TEXT("message"), TEXT(""));
		Response->SetStringField(TEXT("server_id"), ExpectedServerId);
		Response->SetStringField(TEXT("host"), ExpectedHost);
		Response->SetNumberField(TEXT("port"), ExpectedPort);
		Response->SetNumberField(TEXT("heartbeat_interval_seconds"), 10);
		Response->SetNumberField(TEXT("ttl_seconds"), 30);
		return Response;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameServerRegistrationRequestJsonTest,
	"L20260713_Day03.WebApi.GameServerRegistration.RequestJson",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameServerRegistrationRequestJsonTest::RunTest(const FString& Parameters)
{
	const TSharedRef<FJsonObject> Request = MakeGameServerRegistrationRequestJson(ExpectedServerId, ExpectedPort);

	FString ServerId;
	int32 Port = 0;
	TestTrue(TEXT("Request contains server_id"), Request->TryGetStringField(TEXT("server_id"), ServerId));
	TestEqual(TEXT("Request server_id matches"), ServerId, FString(ExpectedServerId));
	TestTrue(TEXT("Request contains port"), Request->TryGetNumberField(TEXT("port"), Port));
	TestEqual(TEXT("Request port matches"), Port, ExpectedPort);
	TestFalse(TEXT("Request does not send host"), Request->HasField(TEXT("host")));
	TestEqual(TEXT("Request has only contract fields"), Request->Values.Num(), 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameServerRegistrationValidResponseTest,
	"L20260713_Day03.WebApi.GameServerRegistration.ValidResponse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameServerRegistrationValidResponseTest::RunTest(const FString& Parameters)
{
	FGameServerRegistrationResponse Parsed;
	const bool bValid = TryParseGameServerRegistrationResponse(
		MakeValidRegistrationResponse(), ExpectedServerId, ExpectedPort, Parsed);

	TestTrue(TEXT("Complete matching response is valid"), bValid);
	TestEqual(TEXT("Parsed server_id matches"), Parsed.ServerId, FString(ExpectedServerId));
	TestEqual(TEXT("Parsed host matches"), Parsed.Host, FString(ExpectedHost));
	TestEqual(TEXT("Parsed port matches"), Parsed.Port, ExpectedPort);
	TestEqual(TEXT("Parsed heartbeat interval matches"), Parsed.HeartbeatIntervalSeconds, 10);
	TestEqual(TEXT("Parsed TTL matches"), Parsed.TtlSeconds, 30);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameServerRegistrationInvalidResponseTest,
	"L20260713_Day03.WebApi.GameServerRegistration.InvalidResponses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameServerRegistrationInvalidResponseTest::RunTest(const FString& Parameters)
{
	auto TestInvalidResponse = [this](const TCHAR* CaseName, const TSharedRef<FJsonObject>& Response)
		{
			FGameServerRegistrationResponse Parsed;
			const bool bValid = TryParseGameServerRegistrationResponse(
				Response, ExpectedServerId, ExpectedPort, Parsed);

			TestFalse(CaseName, bValid);
		};

	TSharedRef<FJsonObject> ResultFalse = MakeValidRegistrationResponse();
	ResultFalse->SetBoolField(TEXT("result"), false);
	TestInvalidResponse(TEXT("result false is invalid"), ResultFalse);

	TSharedRef<FJsonObject> MissingServerId = MakeValidRegistrationResponse();
	MissingServerId->RemoveField(TEXT("server_id"));
	TestInvalidResponse(TEXT("Missing server_id is invalid"), MissingServerId);

	TSharedRef<FJsonObject> MismatchedServerId = MakeValidRegistrationResponse();
	MismatchedServerId->SetStringField(TEXT("server_id"), TEXT("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"));
	TestInvalidResponse(TEXT("Mismatched server_id is invalid"), MismatchedServerId);

	TSharedRef<FJsonObject> MissingHost = MakeValidRegistrationResponse();
	MissingHost->RemoveField(TEXT("host"));
	TestInvalidResponse(TEXT("Missing host is invalid"), MissingHost);

	TSharedRef<FJsonObject> EmptyHost = MakeValidRegistrationResponse();
	EmptyHost->SetStringField(TEXT("host"), TEXT("   "));
	TestInvalidResponse(TEXT("Empty host is invalid"), EmptyHost);

	TSharedRef<FJsonObject> MissingPort = MakeValidRegistrationResponse();
	MissingPort->RemoveField(TEXT("port"));
	TestInvalidResponse(TEXT("Missing port is invalid"), MissingPort);

	TSharedRef<FJsonObject> ZeroPort = MakeValidRegistrationResponse();
	ZeroPort->SetNumberField(TEXT("port"), 0);
	TestInvalidResponse(TEXT("Port zero is invalid"), ZeroPort);

	TSharedRef<FJsonObject> PortAboveMaximum = MakeValidRegistrationResponse();
	PortAboveMaximum->SetNumberField(TEXT("port"), 65536);
	TestInvalidResponse(TEXT("Port above 65535 is invalid"), PortAboveMaximum);

	TSharedRef<FJsonObject> MismatchedPort = MakeValidRegistrationResponse();
	MismatchedPort->SetNumberField(TEXT("port"), 7778);
	TestInvalidResponse(TEXT("Mismatched port is invalid"), MismatchedPort);

	TSharedRef<FJsonObject> MissingHeartbeat = MakeValidRegistrationResponse();
	MissingHeartbeat->RemoveField(TEXT("heartbeat_interval_seconds"));
	TestInvalidResponse(TEXT("Missing heartbeat interval is invalid"), MissingHeartbeat);

	TSharedRef<FJsonObject> ZeroHeartbeat = MakeValidRegistrationResponse();
	ZeroHeartbeat->SetNumberField(TEXT("heartbeat_interval_seconds"), 0);
	TestInvalidResponse(TEXT("Heartbeat interval zero is invalid"), ZeroHeartbeat);

	TSharedRef<FJsonObject> MissingTtl = MakeValidRegistrationResponse();
	MissingTtl->RemoveField(TEXT("ttl_seconds"));
	TestInvalidResponse(TEXT("Missing TTL is invalid"), MissingTtl);

	TSharedRef<FJsonObject> ZeroTtl = MakeValidRegistrationResponse();
	ZeroTtl->SetNumberField(TEXT("ttl_seconds"), 0);
	TestInvalidResponse(TEXT("TTL zero is invalid"), ZeroTtl);

	TSharedRef<FJsonObject> HeartbeatNotBelowTtl = MakeValidRegistrationResponse();
	HeartbeatNotBelowTtl->SetNumberField(TEXT("heartbeat_interval_seconds"), 30);
	TestInvalidResponse(TEXT("Heartbeat interval must be below TTL"), HeartbeatNotBelowTtl);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameServerRegistrationUuidTest,
	"L20260713_Day03.WebApi.GameServerRegistration.HostingUuid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameServerRegistrationUuidTest::RunTest(const FString& Parameters)
{
	const FString ServerId = CreateHostingServerId();
	FGuid ParsedGuid;

	TestEqual(TEXT("Hosting UUID has the standard hyphenated length"), ServerId.Len(), 36);
	TestEqual(TEXT("First UUID separator is present"), ServerId[8], TCHAR('-'));
	TestEqual(TEXT("Second UUID separator is present"), ServerId[13], TCHAR('-'));
	TestEqual(TEXT("Third UUID separator is present"), ServerId[18], TCHAR('-'));
	TestEqual(TEXT("Fourth UUID separator is present"), ServerId[23], TCHAR('-'));
	TestTrue(TEXT("Hosting UUID can be parsed back into FGuid"), FGuid::Parse(ServerId, ParsedGuid));
	TestTrue(TEXT("Hosting UUID is valid"), ParsedGuid.IsValid());

	return true;
}

#endif
