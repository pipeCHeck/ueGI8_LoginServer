#if WITH_DEV_AUTOMATION_TESTS

#include "../Web/WebApiSubsystem.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"

namespace
{
	constexpr TCHAR UnregisterServerId[] = TEXT("123e4567-e89b-12d3-a456-426614174000");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameServerUnregisterSuccessResponseTest,
	"L20260713_Day03.WebApi.GameServerUnregister.SuccessResponse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameServerUnregisterSuccessResponseTest::RunTest(const FString& Parameters)
{
	TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetBoolField(TEXT("result"), true);
	Response->SetStringField(TEXT("message"), TEXT(""));

	TestTrue(
		TEXT("Connected HTTP 200 with result true is unregister success"),
		ClassifyGameServerUnregisterResponse(true, 200, Response)
			== EGameServerUnregisterResult::Success);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameServerUnregisterFailureResponseTest,
	"L20260713_Day03.WebApi.GameServerUnregister.FailureResponses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameServerUnregisterFailureResponseTest::RunTest(const FString& Parameters)
{
	TSharedRef<FJsonObject> SuccessResponse = MakeShared<FJsonObject>();
	SuccessResponse->SetBoolField(TEXT("result"), true);

	TestTrue(
		TEXT("Network failure is unregister failure"),
		ClassifyGameServerUnregisterResponse(false, 0, nullptr)
			== EGameServerUnregisterResult::Failure);
	TestTrue(
		TEXT("HTTP 500 is unregister failure"),
		ClassifyGameServerUnregisterResponse(true, 500, SuccessResponse)
			== EGameServerUnregisterResult::Failure);
	TestTrue(
		TEXT("HTTP 404 is unregister failure"),
		ClassifyGameServerUnregisterResponse(true, 404, SuccessResponse)
			== EGameServerUnregisterResult::Failure);
	TestTrue(
		TEXT("HTTP 200 without JSON is unregister failure"),
		ClassifyGameServerUnregisterResponse(true, 200, nullptr)
			== EGameServerUnregisterResult::Failure);

	const TSharedRef<FJsonObject> MissingResult = MakeShared<FJsonObject>();
	TestTrue(
		TEXT("HTTP 200 without result is unregister failure"),
		ClassifyGameServerUnregisterResponse(true, 200, MissingResult)
			== EGameServerUnregisterResult::Failure);

	TSharedRef<FJsonObject> ResultFalse = MakeShared<FJsonObject>();
	ResultFalse->SetBoolField(TEXT("result"), false);
	TestTrue(
		TEXT("HTTP 200 with result false is unregister failure"),
		ClassifyGameServerUnregisterResponse(true, 200, ResultFalse)
			== EGameServerUnregisterResult::Failure);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameServerUnregisterSessionPolicyTest,
	"L20260713_Day03.WebApi.GameServerUnregister.SessionPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameServerUnregisterSessionPolicyTest::RunTest(const FString& Parameters)
{
	FGuid HostingId;
	TestTrue(TEXT("Test UUID parses"), FGuid::Parse(UnregisterServerId, HostingId));
	TestTrue(
		TEXT("A complete hosting session can be unregistered"),
		IsValidGameServerHostingSession(HostingId, TEXT("192.168.0.10"), 7777));
	TestFalse(
		TEXT("An invalid UUID cannot be unregistered"),
		IsValidGameServerHostingSession(FGuid(), TEXT("192.168.0.10"), 7777));
	TestFalse(
		TEXT("An empty registry address cannot be unregistered"),
		IsValidGameServerHostingSession(HostingId, TEXT("   "), 7777));
	TestFalse(
		TEXT("Port zero cannot be unregistered"),
		IsValidGameServerHostingSession(HostingId, TEXT("192.168.0.10"), 0));
	TestFalse(
		TEXT("Port 65536 cannot be unregistered"),
		IsValidGameServerHostingSession(HostingId, TEXT("192.168.0.10"), 65536));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameServerUnregisterSessionResetTest,
	"L20260713_Day03.WebApi.GameServerUnregister.SessionReset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameServerUnregisterSessionResetTest::RunTest(const FString& Parameters)
{
	FGuid PreviousId;
	TestTrue(TEXT("Previous UUID parses"), FGuid::Parse(UnregisterServerId, PreviousId));
	FGuid CurrentId = PreviousId;
	FString RegistryAddress = TEXT("192.168.0.10");
	int32 HostingPort = 7777;

	ResetGameServerHostingSession(CurrentId, RegistryAddress, HostingPort);

	TestFalse(TEXT("Reset invalidates hosting UUID"), CurrentId.IsValid());
	TestTrue(TEXT("Reset clears registry address"), RegistryAddress.IsEmpty());
	TestEqual(TEXT("Reset clears hosting port"), HostingPort, 0);
	TestFalse(
		TEXT("Reset session is no longer eligible for unregister"),
		IsValidGameServerHostingSession(CurrentId, RegistryAddress, HostingPort));

	const FString NextIdString = CreateHostingServerId();
	FGuid NextId;
	TestTrue(TEXT("A new session can generate a valid UUID"), FGuid::Parse(NextIdString, NextId));
	TestNotEqual(TEXT("A new session does not reuse the stopped UUID"), NextId, PreviousId);

	return true;
}

#endif
