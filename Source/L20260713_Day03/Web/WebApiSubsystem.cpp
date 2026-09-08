// Fill out your copyright notice in the Description page of Project Settings.


#include "WebApiSubsystem.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Engine/GameInstance.h"
#include "../DataGameInstanceSubsystem.h"

namespace
{
	constexpr int32 WebServerPort = 8080;

	bool FailGameServerRegistrationParse(
		FGameServerRegistrationResponse& OutResponse,
		FString* OutError,
		const TCHAR* Error)
	{
		OutResponse = FGameServerRegistrationResponse();
		if (OutError != nullptr)
		{
			*OutError = Error;
		}
		return false;
	}
}

TSharedRef<FJsonObject> MakeGameServerRegistrationRequestJson(
	const FString& InServerId,
	const int32 InPort)
{
	TSharedRef<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	JsonObject->SetStringField(TEXT("server_id"), InServerId);
	JsonObject->SetNumberField(TEXT("port"), InPort);
	return JsonObject;
}

bool TryParseGameServerRegistrationResponse(
	const TSharedPtr<FJsonObject>& InJsonObject,
	const FString& InExpectedServerId,
	const int32 InExpectedPort,
	FGameServerRegistrationResponse& OutResponse,
	FString* OutError)
{
	OutResponse = FGameServerRegistrationResponse();
	if (OutError != nullptr)
	{
		OutError->Reset();
	}

	if (!InJsonObject.IsValid())
	{
		return FailGameServerRegistrationParse(OutResponse, OutError, TEXT("JSON object is missing"));
	}

	bool bResult = false;
	if (!InJsonObject->TryGetBoolField(TEXT("result"), bResult) || !bResult)
	{
		return FailGameServerRegistrationParse(OutResponse, OutError, TEXT("result is missing or false"));
	}

	FGameServerRegistrationResponse Parsed;
	if (!InJsonObject->TryGetStringField(TEXT("server_id"), Parsed.ServerId))
	{
		return FailGameServerRegistrationParse(OutResponse, OutError, TEXT("server_id is missing"));
	}
	if (Parsed.ServerId != InExpectedServerId)
	{
		return FailGameServerRegistrationParse(OutResponse, OutError, TEXT("server_id does not match the hosting session"));
	}

	if (!InJsonObject->TryGetStringField(TEXT("host"), Parsed.Host)
		|| Parsed.Host.TrimStartAndEnd().IsEmpty())
	{
		return FailGameServerRegistrationParse(OutResponse, OutError, TEXT("host is missing or empty"));
	}

	if (!InJsonObject->TryGetNumberField(TEXT("port"), Parsed.Port)
		|| Parsed.Port < 1
		|| Parsed.Port > 65535)
	{
		return FailGameServerRegistrationParse(OutResponse, OutError, TEXT("port is missing or out of range"));
	}
	if (Parsed.Port != InExpectedPort)
	{
		return FailGameServerRegistrationParse(OutResponse, OutError, TEXT("port does not match the Listen Server port"));
	}

	if (!InJsonObject->TryGetNumberField(
		TEXT("heartbeat_interval_seconds"), Parsed.HeartbeatIntervalSeconds)
		|| Parsed.HeartbeatIntervalSeconds <= 0)
	{
		return FailGameServerRegistrationParse(
			OutResponse, OutError, TEXT("heartbeat_interval_seconds is missing or invalid"));
	}

	if (!InJsonObject->TryGetNumberField(TEXT("ttl_seconds"), Parsed.TtlSeconds)
		|| Parsed.TtlSeconds <= 0)
	{
		return FailGameServerRegistrationParse(OutResponse, OutError, TEXT("ttl_seconds is missing or invalid"));
	}
	if (Parsed.HeartbeatIntervalSeconds >= Parsed.TtlSeconds)
	{
		return FailGameServerRegistrationParse(
			OutResponse, OutError, TEXT("heartbeat interval must be lower than TTL"));
	}

	OutResponse = MoveTemp(Parsed);
	return true;
}

FString CreateHostingServerId()
{
	return FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
}

TSharedRef<FJsonObject> MakeGameServerIdentityRequestJson(const FString& InServerId)
{
	TSharedRef<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	JsonObject->SetStringField(TEXT("server_id"), InServerId);
	return JsonObject;
}

bool TryParseRegistrySuccessResponse(const TSharedPtr<FJsonObject>& InJsonObject)
{
	bool bResult = false;
	return InJsonObject.IsValid()
		&& InJsonObject->TryGetBoolField(TEXT("result"), bResult)
		&& bResult;
}

EGameServerMaintenanceAction GetGameServerMaintenanceAction(
	const bool bInRegistered,
	const bool bInHasValidHostingSession)
{
	if (!bInHasValidHostingSession)
	{
		return EGameServerMaintenanceAction::None;
	}

	return bInRegistered
		? EGameServerMaintenanceAction::Heartbeat
		: EGameServerMaintenanceAction::RegistrationRetry;
}

EGameServerHeartbeatResult ClassifyGameServerHeartbeatResponse(
	const bool bInConnectedSuccessfully,
	const int32 InResponseCode,
	const TSharedPtr<FJsonObject>& InJsonObject)
{
	if (!bInConnectedSuccessfully)
	{
		return EGameServerHeartbeatResult::TransientFailure;
	}

	if (InResponseCode == 404)
	{
		return EGameServerHeartbeatResult::NotRegistered;
	}

	if (InResponseCode == 200 && TryParseRegistrySuccessResponse(InJsonObject))
	{
		return EGameServerHeartbeatResult::Success;
	}

	return EGameServerHeartbeatResult::TransientFailure;
}

EGameServerUnregisterResult ClassifyGameServerUnregisterResponse(
	const bool bInConnectedSuccessfully,
	const int32 InResponseCode,
	const TSharedPtr<FJsonObject>& InJsonObject)
{
	return bInConnectedSuccessfully
		&& InResponseCode == 200
		&& TryParseRegistrySuccessResponse(InJsonObject)
		? EGameServerUnregisterResult::Success
		: EGameServerUnregisterResult::Failure;
}

bool IsValidGameServerHostingSession(
	const FGuid& InHostingServerId,
	const FString& InRegistryWebServerIP,
	const int32 InHostingGameServerPort)
{
	return InHostingServerId.IsValid()
		&& !InRegistryWebServerIP.TrimStartAndEnd().IsEmpty()
		&& InHostingGameServerPort >= 1
		&& InHostingGameServerPort <= 65535;
}

void ResetGameServerHostingSession(
	FGuid& InOutHostingServerId,
	FString& InOutRegistryWebServerIP,
	int32& InOutHostingGameServerPort)
{
	InOutHostingServerId = FGuid();
	InOutRegistryWebServerIP.Reset();
	InOutHostingGameServerPort = 0;
}

ELoginGameServerParseResult ApplyGameServerFromLoginResponse(
	const TSharedPtr<FJsonObject>& InJsonObject,
	UDataGameInstanceSubsystem& InOutData)
{
	InOutData.ClearGameServer();

	if (!InJsonObject.IsValid())
	{
		return ELoginGameServerParseResult::Invalid;
	}

	const TSharedPtr<FJsonValue> GameServerValue = InJsonObject->TryGetField(TEXT("game_server"));
	if (!GameServerValue.IsValid() || GameServerValue->IsNull())
	{
		return ELoginGameServerParseResult::NotPresent;
	}

	const TSharedPtr<FJsonObject>* GameServerObject = nullptr;
	if (!InJsonObject->TryGetObjectField(TEXT("game_server"), GameServerObject)
		|| GameServerObject == nullptr
		|| !GameServerObject->IsValid())
	{
		return ELoginGameServerParseResult::Invalid;
	}

	FString ServerId;
	FString Host;
	int32 Port = 0;
	if (!(*GameServerObject)->TryGetStringField(TEXT("server_id"), ServerId)
		|| !(*GameServerObject)->TryGetStringField(TEXT("host"), Host)
		|| !(*GameServerObject)->TryGetNumberField(TEXT("port"), Port))
	{
		return ELoginGameServerParseResult::Invalid;
	}

	InOutData.SetGameServer(Host, Port, ServerId);
	return InOutData.HasValidGameServer()
		? ELoginGameServerParseResult::Valid
		: ELoginGameServerParseResult::Invalid;
}

void UWebApiSubsystem::RequestLogin(const FString& InServerIP, const FString& InUserID, const FString& InPassword)
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UDataGameInstanceSubsystem* Data = GameInstance->GetSubsystem<UDataGameInstanceSubsystem>())
		{
			Data->ClearGameServer();
		}
	}

	SendAuthRequest(InServerIP, TEXT("/login"), InUserID, InPassword, OnLoginResult, true);
}

void UWebApiSubsystem::RequestSignUp(const FString& InServerIP, const FString& InUserID, const FString& InPassword)
{
	SendAuthRequest(InServerIP, TEXT("/signup"), InUserID, InPassword, OnSignUpResult, false);
}

void UWebApiSubsystem::StartGameServerRegistration(
	const FString& InWebServerIP,
	const int32 InGameServerPort)
{
	if (bIsDeinitializing)
	{
		return;
	}

	const FString TrimmedWebServerIP = InWebServerIP.TrimStartAndEnd();
	if (TrimmedWebServerIP.IsEmpty() || InGameServerPort < 1 || InGameServerPort > 65535)
	{
		UE_LOG(LogTemp, Warning, TEXT("게임 서버 등록을 시작할 수 없습니다: FastAPI 주소 또는 Listen 포트가 올바르지 않습니다"));
		return;
	}

	if (bRegistrationRequestInFlight)
	{
		UE_LOG(LogTemp, Warning, TEXT("게임 서버 등록 요청이 이미 진행 중입니다"));
		return;
	}

	if (bGameServerRegistered)
	{
		return;
	}

	bHostingShutdownRequested = false;

	if (!HostingServerId.IsValid())
	{
		FGuid::Parse(CreateHostingServerId(), HostingServerId);
	}

	RegistryWebServerIP = TrimmedWebServerIP;
	HostingGameServerPort = InGameServerPort;
	HeartbeatIntervalSeconds = 0;
	RegistryTtlSeconds = 0;
	bGameServerRegistered = false;
	SendGameServerRegistrationRequest();
}

void UWebApiSubsystem::SendGameServerRegistrationRequest()
{
	if (bIsDeinitializing || bHostingShutdownRequested)
	{
		return;
	}

	if (bRegistrationRequestInFlight)
	{
		return;
	}

	if (!HasValidHostingSession())
	{
		UE_LOG(LogTemp, Warning, TEXT("게임 서버 등록 요청을 보낼 수 없습니다: hosting session이 올바르지 않습니다"));
		return;
	}

	const FString HostingServerIdString = HostingServerId.ToString(EGuidFormats::DigitsWithHyphensLower);
	const TSharedRef<FJsonObject> JsonObject = MakeGameServerRegistrationRequestJson(
		HostingServerIdString, HostingGameServerPort);

	FString Body;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Body);
	FJsonSerializer::Serialize(JsonObject, Writer);

	const FString Url = FString::Printf(
		TEXT("http://%s:%d/game-servers/register"), *RegistryWebServerIP, WebServerPort);

	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Url);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetContentAsString(Body);

	bRegistrationRequestInFlight = true;
	ActiveRegistrationRequest = Request;
	TWeakObjectPtr<UWebApiSubsystem> WeakThis(this);
	Request->OnProcessRequestComplete().BindLambda(
		[WeakThis, HostingServerIdString, ExpectedPort = HostingGameServerPort](
			FHttpRequestPtr InRequest,
			FHttpResponsePtr InResponse,
			const bool bInConnectedSuccessfully)
		{
			if (!WeakThis.IsValid()
				|| WeakThis->bIsDeinitializing
				|| WeakThis->bHostingShutdownRequested
				|| WeakThis->ActiveRegistrationRequest != InRequest
				|| WeakThis->HostingServerId.ToString(EGuidFormats::DigitsWithHyphensLower)
					!= HostingServerIdString)
			{
				return;
			}

			WeakThis->ActiveRegistrationRequest.Reset();
			WeakThis->bRegistrationRequestInFlight = false;
			WeakThis->HandleGameServerRegistrationResponse(
				InResponse, bInConnectedSuccessfully, HostingServerIdString, ExpectedPort);
		});

	if (!Request->ProcessRequest())
	{
		ActiveRegistrationRequest.Reset();
		bRegistrationRequestInFlight = false;
		UE_LOG(LogTemp, Warning, TEXT("게임 서버 등록 HTTP 요청을 시작하지 못했습니다"));
	}
}

void UWebApiSubsystem::StopGameServerRegistration()
{
	// Block callbacks and maintenance before cancelling any in-flight work.
	bHostingShutdownRequested = true;
	StopGameServerHeartbeatMaintenance();

	if (ActiveHeartbeatRequest.IsValid())
	{
		ActiveHeartbeatRequest->CancelRequest();
		ActiveHeartbeatRequest.Reset();
	}
	if (ActiveRegistrationRequest.IsValid())
	{
		ActiveRegistrationRequest->CancelRequest();
		ActiveRegistrationRequest.Reset();
	}

	const FGuid StoppedHostingServerId = HostingServerId;
	const FString StoppedRegistryWebServerIP = RegistryWebServerIP;
	const int32 StoppedHostingGameServerPort = HostingGameServerPort;

	bHeartbeatRequestInFlight = false;
	bRegistrationRequestInFlight = false;
	bGameServerRegistered = false;
	HeartbeatIntervalSeconds = 0;
	RegistryTtlSeconds = 0;

	if (IsValidGameServerHostingSession(
		StoppedHostingServerId,
		StoppedRegistryWebServerIP,
		StoppedHostingGameServerPort))
	{
		SendGameServerUnregisterRequest(
			StoppedRegistryWebServerIP,
			StoppedHostingServerId.ToString(EGuidFormats::DigitsWithHyphensLower));
	}

	// Clearing the identity also makes repeated Stop/Deinitialize calls idempotent.
	ResetGameServerHostingSession(
		HostingServerId,
		RegistryWebServerIP,
		HostingGameServerPort);
}

void UWebApiSubsystem::SendGameServerUnregisterRequest(
	const FString& InRegistryWebServerIP,
	const FString& InHostingServerId)
{
	const TSharedRef<FJsonObject> JsonObject = MakeGameServerIdentityRequestJson(InHostingServerId);

	FString Body;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Body);
	FJsonSerializer::Serialize(JsonObject, Writer);

	const FString Url = FString::Printf(
		TEXT("http://%s:%d/game-servers/unregister"), *InRegistryWebServerIP, WebServerPort);

	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Url);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetContentAsString(Body);

	// The subsystem may already be gone when this best-effort request completes.
	Request->OnProcessRequestComplete().BindLambda(
		[](FHttpRequestPtr, FHttpResponsePtr InResponse, const bool bInConnectedSuccessfully)
		{
			const bool bHasResponse = InResponse.IsValid();
			const int32 ResponseCode = bHasResponse ? InResponse->GetResponseCode() : 0;
			TSharedPtr<FJsonObject> ResponseJson;

			if (bInConnectedSuccessfully && bHasResponse && ResponseCode == 200)
			{
				const TSharedRef<TJsonReader<>> Reader =
					TJsonReaderFactory<>::Create(InResponse->GetContentAsString());
				FJsonSerializer::Deserialize(Reader, ResponseJson);
			}

			if (ClassifyGameServerUnregisterResponse(
				bInConnectedSuccessfully && bHasResponse,
				ResponseCode,
				ResponseJson) == EGameServerUnregisterResult::Success)
			{
				UE_LOG(LogTemp, Log, TEXT("게임 서버 unregister 요청이 성공했습니다"));
				return;
			}

			if (!bInConnectedSuccessfully || !bHasResponse)
			{
				UE_LOG(LogTemp, Warning, TEXT("게임 서버 unregister 실패: FastAPI 웹서버에 연결할 수 없습니다"));
			}
			else if (ResponseCode != 200)
			{
				UE_LOG(LogTemp, Warning, TEXT("게임 서버 unregister 실패: HTTP 상태 코드 %d"), ResponseCode);
			}
			else if (!ResponseJson.IsValid())
			{
				UE_LOG(LogTemp, Warning, TEXT("게임 서버 unregister 실패: 응답 JSON을 해석할 수 없습니다"));
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("게임 서버 unregister 실패: result가 true가 아닙니다"));
			}
		});

	if (!Request->ProcessRequest())
	{
		UE_LOG(LogTemp, Warning, TEXT("게임 서버 unregister HTTP 요청을 시작하지 못했습니다"));
	}
}

void UWebApiSubsystem::Deinitialize()
{
	bIsDeinitializing = true;
	StopGameServerRegistration();
	Super::Deinitialize();
}

void UWebApiSubsystem::SendAuthRequest(const FString& InServerIP, const FString& InPath,
	const FString& InUserID, const FString& InPassword,
	FWebApiResultSignature& InDelegate, const bool bInIsLogin)
{
	TSharedRef<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	JsonObject->SetStringField(TEXT("user_id"), InUserID);
	JsonObject->SetStringField(TEXT("passwd"), InPassword);

	FString Body;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Body);
	FJsonSerializer::Serialize(JsonObject, Writer);

	const FString Url = FString::Printf(TEXT("http://%s:%d%s"), *InServerIP, WebServerPort, *InPath);
	UE_LOG(LogTemp, Warning, TEXT("%s"), *Url);

	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Url);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetContentAsString(Body);

	// 응답이 도착하기 전에 GameInstance가 정리될 수 있으므로 약참조로 잡는다.
	TWeakObjectPtr<UWebApiSubsystem> WeakThis(this);
	FWebApiResultSignature* DelegatePtr = &InDelegate;

	Request->OnProcessRequestComplete().BindLambda(
		[WeakThis, DelegatePtr, bInIsLogin](FHttpRequestPtr, FHttpResponsePtr InResponse, bool bInConnectedSuccessfully)
		{
			if (!WeakThis.IsValid())
			{
				return;
			}

			WeakThis->HandleAuthResponse(InResponse, bInConnectedSuccessfully, *DelegatePtr, bInIsLogin);
		});

	Request->ProcessRequest();
}

void UWebApiSubsystem::HandleAuthResponse(FHttpResponsePtr InResponse, const bool bInConnectedSuccessfully,
	FWebApiResultSignature& InDelegate, const bool bInIsLogin)
{
	if (!bInConnectedSuccessfully || !InResponse.IsValid())
	{
		InDelegate.Broadcast(false, TEXT("서버에 연결할 수 없습니다"));
		return;
	}

	const int32 ResponseCode = InResponse->GetResponseCode();
	if (ResponseCode != 200)
	{
		InDelegate.Broadcast(false, FString::Printf(TEXT("요청을 처리할 수 없습니다 (코드 %d)"), ResponseCode));
		return;
	}

	const FString ResponseBody = InResponse->GetContentAsString();

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		InDelegate.Broadcast(false, TEXT("응답을 해석할 수 없습니다"));
		return;
	}

	if (!JsonObject->GetBoolField(TEXT("result")))
	{
		InDelegate.Broadcast(false, JsonObject->GetStringField(TEXT("message")));
		return;
	}

	if (bInIsLogin)
	{
		UDataGameInstanceSubsystem* Data = GetGameInstance()->GetSubsystem<UDataGameInstanceSubsystem>();
		if (Data)
		{
			Data->Idx = JsonObject->GetIntegerField(TEXT("idx"));
			Data->Nickname = JsonObject->GetStringField(TEXT("nickname"));
			Data->Level = JsonObject->GetIntegerField(TEXT("level"));
			Data->bLoggedIn = true;

			if (ApplyGameServerFromLoginResponse(JsonObject, *Data) == ELoginGameServerParseResult::Invalid)
			{
				UE_LOG(LogTemp, Warning, TEXT("로그인 응답의 game_server 정보가 올바르지 않습니다"));
			}
		}
	}

	InDelegate.Broadcast(true, TEXT(""));
}

void UWebApiSubsystem::HandleGameServerRegistrationResponse(
	FHttpResponsePtr InResponse,
	const bool bInConnectedSuccessfully,
	const FString& InExpectedServerId,
	const int32 InExpectedPort)
{
	bGameServerRegistered = false;

	if (!bInConnectedSuccessfully || !InResponse.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("게임 서버 등록 실패: FastAPI 웹서버에 연결할 수 없습니다"));
		return;
	}

	if (InResponse->GetResponseCode() != 200)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("게임 서버 등록 실패: HTTP 상태 코드 %d"),
			InResponse->GetResponseCode());
		return;
	}

	TSharedPtr<FJsonObject> JsonObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InResponse->GetContentAsString());
	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("게임 서버 등록 실패: 응답 JSON을 해석할 수 없습니다"));
		return;
	}

	FGameServerRegistrationResponse Parsed;
	FString ValidationError;
	if (!TryParseGameServerRegistrationResponse(
		JsonObject, InExpectedServerId, InExpectedPort, Parsed, &ValidationError))
	{
		UE_LOG(LogTemp, Warning, TEXT("게임 서버 등록 실패: %s"), *ValidationError);
		return;
	}

	bGameServerRegistered = true;
	HeartbeatIntervalSeconds = Parsed.HeartbeatIntervalSeconds;
	RegistryTtlSeconds = Parsed.TtlSeconds;
	StartGameServerHeartbeatMaintenance();
	UE_LOG(
		LogTemp,
		Log,
		TEXT("게임 서버 등록 성공: %s:%d (heartbeat=%d, ttl=%d)"),
		*Parsed.Host,
		Parsed.Port,
		HeartbeatIntervalSeconds,
		RegistryTtlSeconds);
}

bool UWebApiSubsystem::HasValidHostingSession() const
{
	return IsValidGameServerHostingSession(
		HostingServerId,
		RegistryWebServerIP,
		HostingGameServerPort);
}

void UWebApiSubsystem::StartGameServerHeartbeatMaintenance()
{
	if (bIsDeinitializing || bHostingShutdownRequested || HeartbeatIntervalSeconds <= 0)
	{
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	if (GameInstance == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("heartbeat maintenance를 시작할 수 없습니다: GameInstance가 없습니다"));
		return;
	}

	FTimerManager& TimerManager = GameInstance->GetTimerManager();
	TimerManager.ClearTimer(HeartbeatTimerHandle);
	const float Interval = static_cast<float>(HeartbeatIntervalSeconds);
	TimerManager.SetTimer(
		HeartbeatTimerHandle,
		this,
		&UWebApiSubsystem::HandleGameServerMaintenanceTick,
		Interval,
		true,
		Interval);
}

void UWebApiSubsystem::StopGameServerHeartbeatMaintenance()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		GameInstance->GetTimerManager().ClearTimer(HeartbeatTimerHandle);
	}
	else
	{
		HeartbeatTimerHandle.Invalidate();
	}
}

void UWebApiSubsystem::HandleGameServerMaintenanceTick()
{
	if (bIsDeinitializing || bHostingShutdownRequested)
	{
		return;
	}

	const EGameServerMaintenanceAction Action = GetGameServerMaintenanceAction(
		bGameServerRegistered, HasValidHostingSession());

	switch (Action)
	{
	case EGameServerMaintenanceAction::Heartbeat:
		SendGameServerHeartbeat();
		break;

	case EGameServerMaintenanceAction::RegistrationRetry:
		SendGameServerRegistrationRequest();
		break;

	case EGameServerMaintenanceAction::None:
	default:
		break;
	}
}

void UWebApiSubsystem::SendGameServerHeartbeat()
{
	if (bIsDeinitializing
		|| bHostingShutdownRequested
		|| bHeartbeatRequestInFlight
		|| !HasValidHostingSession())
	{
		return;
	}

	const FString HostingServerIdString = HostingServerId.ToString(EGuidFormats::DigitsWithHyphensLower);
	const TSharedRef<FJsonObject> JsonObject = MakeGameServerIdentityRequestJson(HostingServerIdString);

	FString Body;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Body);
	FJsonSerializer::Serialize(JsonObject, Writer);

	const FString Url = FString::Printf(
		TEXT("http://%s:%d/game-servers/heartbeat"), *RegistryWebServerIP, WebServerPort);

	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Url);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetContentAsString(Body);

	bHeartbeatRequestInFlight = true;
	ActiveHeartbeatRequest = Request;
	TWeakObjectPtr<UWebApiSubsystem> WeakThis(this);
	Request->OnProcessRequestComplete().BindLambda(
		[WeakThis, HostingServerIdString](
			FHttpRequestPtr InRequest,
			FHttpResponsePtr InResponse,
			const bool bInConnectedSuccessfully)
		{
			if (!WeakThis.IsValid()
				|| WeakThis->bIsDeinitializing
				|| WeakThis->bHostingShutdownRequested
				|| WeakThis->ActiveHeartbeatRequest != InRequest
				|| WeakThis->HostingServerId.ToString(EGuidFormats::DigitsWithHyphensLower)
					!= HostingServerIdString)
			{
				return;
			}

			WeakThis->ActiveHeartbeatRequest.Reset();
			WeakThis->bHeartbeatRequestInFlight = false;
			WeakThis->HandleGameServerHeartbeatResponse(InResponse, bInConnectedSuccessfully);
		});

	if (!Request->ProcessRequest())
	{
		ActiveHeartbeatRequest.Reset();
		bHeartbeatRequestInFlight = false;
		UE_LOG(LogTemp, Warning, TEXT("게임 서버 heartbeat HTTP 요청을 시작하지 못했습니다"));
	}
}

void UWebApiSubsystem::HandleGameServerHeartbeatResponse(
	FHttpResponsePtr InResponse,
	const bool bInConnectedSuccessfully)
{
	const bool bHasResponse = InResponse.IsValid();
	const int32 ResponseCode = bHasResponse ? InResponse->GetResponseCode() : 0;
	TSharedPtr<FJsonObject> JsonObject;

	if (bInConnectedSuccessfully && bHasResponse && ResponseCode == 200)
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InResponse->GetContentAsString());
		FJsonSerializer::Deserialize(Reader, JsonObject);
	}

	const EGameServerHeartbeatResult Result = ClassifyGameServerHeartbeatResponse(
		bInConnectedSuccessfully && bHasResponse, ResponseCode, JsonObject);

	switch (Result)
	{
	case EGameServerHeartbeatResult::Success:
		bGameServerRegistered = true;
		return;

	case EGameServerHeartbeatResult::NotRegistered:
		bGameServerRegistered = false;
		UE_LOG(LogTemp, Warning, TEXT("게임 서버 heartbeat가 404를 반환했습니다: 같은 hosting UUID로 재등록합니다"));
		SendGameServerRegistrationRequest();
		return;

	case EGameServerHeartbeatResult::TransientFailure:
	default:
		if (!bInConnectedSuccessfully || !bHasResponse)
		{
			UE_LOG(LogTemp, Warning, TEXT("게임 서버 heartbeat 실패: FastAPI 웹서버에 연결할 수 없습니다"));
		}
		else if (ResponseCode != 200)
		{
			UE_LOG(LogTemp, Warning, TEXT("게임 서버 heartbeat 실패: HTTP 상태 코드 %d"), ResponseCode);
		}
		else if (!JsonObject.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("게임 서버 heartbeat 실패: 응답 JSON을 해석할 수 없습니다"));
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("게임 서버 heartbeat 실패: result가 true가 아닙니다"));
		}
		return;
	}
}
