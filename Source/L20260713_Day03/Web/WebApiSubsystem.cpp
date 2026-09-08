// Fill out your copyright notice in the Description page of Project Settings.


#include "WebApiSubsystem.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
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

	if (!HostingServerId.IsValid())
	{
		FGuid::Parse(CreateHostingServerId(), HostingServerId);
	}

	RegistryWebServerIP = TrimmedWebServerIP;
	HostingGameServerPort = InGameServerPort;
	HeartbeatIntervalSeconds = 0;
	RegistryTtlSeconds = 0;
	bGameServerRegistered = false;

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
	TWeakObjectPtr<UWebApiSubsystem> WeakThis(this);
	Request->OnProcessRequestComplete().BindLambda(
		[WeakThis, HostingServerIdString, ExpectedPort = HostingGameServerPort](
			FHttpRequestPtr,
			FHttpResponsePtr InResponse,
			const bool bInConnectedSuccessfully)
		{
			if (!WeakThis.IsValid())
			{
				return;
			}

			WeakThis->bRegistrationRequestInFlight = false;
			WeakThis->HandleGameServerRegistrationResponse(
				InResponse, bInConnectedSuccessfully, HostingServerIdString, ExpectedPort);
		});

	if (!Request->ProcessRequest())
	{
		bRegistrationRequestInFlight = false;
		UE_LOG(LogTemp, Warning, TEXT("게임 서버 등록 HTTP 요청을 시작하지 못했습니다"));
	}
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
	HeartbeatIntervalSeconds = 0;
	RegistryTtlSeconds = 0;

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
	UE_LOG(
		LogTemp,
		Log,
		TEXT("게임 서버 등록 성공: %s:%d (heartbeat=%d, ttl=%d)"),
		*Parsed.Host,
		Parsed.Port,
		HeartbeatIntervalSeconds,
		RegistryTtlSeconds);
}
