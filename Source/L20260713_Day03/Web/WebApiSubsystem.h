// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/IHttpRequest.h"
#include "TimerManager.h"
#include "WebApiSubsystem.generated.h"

class FJsonObject;
class UDataGameInstanceSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FWebApiResultSignature, const bool, bInSuccess, const FString&, InMessage);

enum class ELoginGameServerParseResult : uint8
{
	NotPresent,
	Valid,
	Invalid,
};

enum class EGameServerMaintenanceAction : uint8
{
	None,
	Heartbeat,
	RegistrationRetry,
};

enum class EGameServerHeartbeatResult : uint8
{
	Success,
	NotRegistered,
	TransientFailure,
};

struct FGameServerRegistrationResponse
{
	FString ServerId;
	FString Host;
	int32 Port = 0;
	int32 HeartbeatIntervalSeconds = 0;
	int32 TtlSeconds = 0;
};

ELoginGameServerParseResult ApplyGameServerFromLoginResponse(
	const TSharedPtr<FJsonObject>& InJsonObject,
	UDataGameInstanceSubsystem& InOutData);

TSharedRef<FJsonObject> MakeGameServerRegistrationRequestJson(
	const FString& InServerId,
	int32 InPort);

bool TryParseGameServerRegistrationResponse(
	const TSharedPtr<FJsonObject>& InJsonObject,
	const FString& InExpectedServerId,
	int32 InExpectedPort,
	FGameServerRegistrationResponse& OutResponse,
	FString* OutError = nullptr);

FString CreateHostingServerId();

TSharedRef<FJsonObject> MakeGameServerIdentityRequestJson(const FString& InServerId);

bool TryParseRegistrySuccessResponse(const TSharedPtr<FJsonObject>& InJsonObject);

EGameServerMaintenanceAction GetGameServerMaintenanceAction(
	bool bInRegistered,
	bool bInHasValidHostingSession);

EGameServerHeartbeatResult ClassifyGameServerHeartbeatResponse(
	bool bInConnectedSuccessfully,
	int32 InResponseCode,
	const TSharedPtr<FJsonObject>& InJsonObject);

/**
 * 웹서버와의 HTTP 통신을 전담한다. 결과는 델리게이트로만 알린다.
 */
UCLASS()
class L20260713_DAY03_API UWebApiSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:

	UPROPERTY(BlueprintAssignable, Category = "WebApi")
	FWebApiResultSignature OnLoginResult;

	UPROPERTY(BlueprintAssignable, Category = "WebApi")
	FWebApiResultSignature OnSignUpResult;

	void RequestLogin(const FString& InServerIP, const FString& InUserID, const FString& InPassword);

	void RequestSignUp(const FString& InServerIP, const FString& InUserID, const FString& InPassword);

	void StartGameServerRegistration(const FString& InWebServerIP, int32 InGameServerPort);

	virtual void Deinitialize() override;

private:

	void SendAuthRequest(const FString& InServerIP, const FString& InPath,
		const FString& InUserID, const FString& InPassword,
		FWebApiResultSignature& InDelegate, const bool bInIsLogin);

	void HandleAuthResponse(FHttpResponsePtr InResponse, const bool bInConnectedSuccessfully,
		FWebApiResultSignature& InDelegate, const bool bInIsLogin);

	void HandleGameServerRegistrationResponse(
		FHttpResponsePtr InResponse,
		bool bInConnectedSuccessfully,
		const FString& InExpectedServerId,
		int32 InExpectedPort);

	void SendGameServerRegistrationRequest();
	void StartGameServerHeartbeatMaintenance();
	void StopGameServerHeartbeatMaintenance();
	void HandleGameServerMaintenanceTick();
	void SendGameServerHeartbeat();
	void HandleGameServerHeartbeatResponse(
		FHttpResponsePtr InResponse,
		bool bInConnectedSuccessfully);
	bool HasValidHostingSession() const;

	// Current process's hosting ID. This is separate from the login-discovered GameServerId.
	FGuid HostingServerId;

	// FastAPI registry address and the actual port bound by this Listen Server.
	FString RegistryWebServerIP;
	int32 HostingGameServerPort = 0;

	bool bRegistrationRequestInFlight = false;
	bool bGameServerRegistered = false;
	bool bHeartbeatRequestInFlight = false;
	bool bIsDeinitializing = false;

	// Registry lease values returned by register; the interval drives GameInstance-owned maintenance.
	int32 HeartbeatIntervalSeconds = 0;
	int32 RegistryTtlSeconds = 0;
	FTimerHandle HeartbeatTimerHandle;
};
