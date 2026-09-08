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
