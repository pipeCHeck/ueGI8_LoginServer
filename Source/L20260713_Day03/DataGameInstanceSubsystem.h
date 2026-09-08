// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DataGameInstanceSubsystem.generated.h"

/**
 * 
 */
UCLASS()
class L20260713_DAY03_API UDataGameInstanceSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	virtual void Deinitialize() override;

	void ClearGameServer();

	void SetGameServer(const FString& InHost, int32 InPort, const FString& InServerId);

	bool HasValidGameServer() const;

	FString GetGameServerAddress() const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
	FString UserID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
	FString Password;

	// FastAPI 웹서버의 호스트 또는 IP 주소.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
	FString ServerIP;

	// 로그인 응답으로 검색된 접속 대상 Unreal 게임 서버의 호스트.
	UPROPERTY(BlueprintReadOnly, Category = "Data")
	FString GameServerHost;

	// 로그인 응답으로 검색된 접속 대상 Unreal 게임 서버의 포트.
	UPROPERTY(BlueprintReadOnly, Category = "Data")
	int32 GameServerPort = 0;

	// 로그인 응답으로 검색된 접속 대상 게임 서버 ID. Listen Server 자신의 등록 UUID가 아니다.
	UPROPERTY(BlueprintReadOnly, Category = "Data")
	FString GameServerId;

	UPROPERTY(BlueprintReadOnly, Category = "Data")
	bool bLoggedIn = false;

	UPROPERTY(BlueprintReadOnly, Category = "Data")
	int32 Idx = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Data")
	FString Nickname;

	UPROPERTY(BlueprintReadOnly, Category = "Data")
	int32 Level = 0;

};
