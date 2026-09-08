#if WITH_DEV_AUTOMATION_TESTS

#include "../Web/WebApiSubsystem.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"

namespace
{
	constexpr TCHAR HeartbeatServerId[] = TEXT("123e4567-e89b-12d3-a456-426614174000");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameServerHeartbeatRequestJsonTest,
	"L20260713_Day03.WebApi.GameServerHeartbeat.RequestJson",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameServerHeartbeatRequestJsonTest::RunTest(const FString& Parameters)
{
	const TSharedRef<FJsonObject> Request = MakeGameServerIdentityRequestJson(HeartbeatServerId);

	FString ServerId;
	TestTrue(TEXT("Heartbeat request contains server_id"), Request->TryGetStringField(TEXT("server_id"), ServerId));
	TestEqual(TEXT("Heartbeat request preserves server_id"), ServerId, FString(HeartbeatServerId));
	TestFalse(TEXT("Heartbeat request does not contain host"), Request->HasField(TEXT("host")));
	TestFalse(TEXT("Heartbeat request does not contain port"), Request->HasField(TEXT("port")));
	TestEqual(TEXT("Heartbeat request has exactly one field"), Request->Values.Num(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameServerHeartbeatSuccessResponseTest,
	"L20260713_Day03.WebApi.GameServerHeartbeat.SuccessResponse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameServerHeartbeatSuccessResponseTest::RunTest(const FString& Parameters)
{
	TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetBoolField(TEXT("result"), true);
	Response->SetStringField(TEXT("message"), TEXT(""));

	TestTrue(TEXT("result true is a successful registry response"), TryParseRegistrySuccessResponse(Response));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameServerHeartbeatInvalidResponseTest,
	"L20260713_Day03.WebApi.GameServerHeartbeat.InvalidResponses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameServerHeartbeatInvalidResponseTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> InvalidResponse;
	TestFalse(TEXT("A null JSON object is invalid"), TryParseRegistrySuccessResponse(InvalidResponse));

	const TSharedRef<FJsonObject> MissingResult = MakeShared<FJsonObject>();
	TestFalse(TEXT("A missing result field is invalid"), TryParseRegistrySuccessResponse(MissingResult));

	TSharedRef<FJsonObject> ResultFalse = MakeShared<FJsonObject>();
	ResultFalse->SetBoolField(TEXT("result"), false);
	TestFalse(TEXT("result false is invalid"), TryParseRegistrySuccessResponse(ResultFalse));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameServerHeartbeatMaintenanceActionTest,
	"L20260713_Day03.WebApi.GameServerHeartbeat.MaintenanceAction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameServerHeartbeatMaintenanceActionTest::RunTest(const FString& Parameters)
{
	TestTrue(
		TEXT("A registered valid session sends heartbeat"),
		GetGameServerMaintenanceAction(true, true) == EGameServerMaintenanceAction::Heartbeat);
	TestTrue(
		TEXT("An unregistered valid session retries registration"),
		GetGameServerMaintenanceAction(false, true) == EGameServerMaintenanceAction::RegistrationRetry);
	TestTrue(
		TEXT("A missing hosting session performs no maintenance"),
		GetGameServerMaintenanceAction(false, false) == EGameServerMaintenanceAction::None);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameServerHeartbeatResultClassificationTest,
	"L20260713_Day03.WebApi.GameServerHeartbeat.ResultClassification",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameServerHeartbeatResultClassificationTest::RunTest(const FString& Parameters)
{
	TSharedRef<FJsonObject> SuccessResponse = MakeShared<FJsonObject>();
	SuccessResponse->SetBoolField(TEXT("result"), true);

	TestTrue(
		TEXT("HTTP 200 with result true is success"),
		ClassifyGameServerHeartbeatResponse(true, 200, SuccessResponse)
			== EGameServerHeartbeatResult::Success);
	TestTrue(
		TEXT("HTTP 404 is not registered"),
		ClassifyGameServerHeartbeatResponse(true, 404, nullptr)
			== EGameServerHeartbeatResult::NotRegistered);
	TestTrue(
		TEXT("Network failure is transient"),
		ClassifyGameServerHeartbeatResponse(false, 0, nullptr)
			== EGameServerHeartbeatResult::TransientFailure);
	TestTrue(
		TEXT("HTTP 500 is transient"),
		ClassifyGameServerHeartbeatResponse(true, 500, nullptr)
			== EGameServerHeartbeatResult::TransientFailure);

	TSharedRef<FJsonObject> ResultFalse = MakeShared<FJsonObject>();
	ResultFalse->SetBoolField(TEXT("result"), false);
	TestTrue(
		TEXT("HTTP 200 with result false is transient"),
		ClassifyGameServerHeartbeatResponse(true, 200, ResultFalse)
			== EGameServerHeartbeatResult::TransientFailure);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameServerHeartbeatRegistrationRetryIdentityTest,
	"L20260713_Day03.WebApi.GameServerHeartbeat.RegistrationRetryPreservesIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameServerHeartbeatRegistrationRetryIdentityTest::RunTest(const FString& Parameters)
{
	const TSharedRef<FJsonObject> HeartbeatRequest = MakeGameServerIdentityRequestJson(HeartbeatServerId);
	const TSharedRef<FJsonObject> RegistrationRetry = MakeGameServerRegistrationRequestJson(HeartbeatServerId, 7777);

	FString HeartbeatId;
	FString RegistrationId;
	HeartbeatRequest->TryGetStringField(TEXT("server_id"), HeartbeatId);
	RegistrationRetry->TryGetStringField(TEXT("server_id"), RegistrationId);

	TestEqual(TEXT("Registration retry reuses the heartbeat session UUID"), RegistrationId, HeartbeatId);
	TestEqual(TEXT("Registration retry keeps the original hosting UUID"), RegistrationId, FString(HeartbeatServerId));

	return true;
}

#endif
