// Fill out your copyright notice in the Description page of Project Settings.


#include "LobbyGM.h"
#include "Kismet/KismetSystemLibrary.h"
#include "LobbyGS.h"
#include "LobbyPC.h"
#include "Engine/GameInstance.h"
#include "Engine/NetDriver.h"
#include "../DataGameInstanceSubsystem.h"
#include "../Web/WebApiSubsystem.h"

void ALobbyGM::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	//UKismetSystemLibrary::PrintString(GetWorld(), TEXT("ALobbyGM::PreLogin Begin"));

	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);

	//UKismetSystemLibrary::PrintString(GetWorld(), TEXT("ALobbyGM::PreLogin End"));
}

APlayerController* ALobbyGM::Login(UPlayer* NewPlayer, ENetRole InRemoteRole, const FString& Portal, const FString& Options, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	//UKismetSystemLibrary::PrintString(GetWorld(), TEXT("ALobbyGM::Login Begin"));

	APlayerController* PC =  Super::Login(NewPlayer, InRemoteRole, Portal, Options, UniqueId, ErrorMessage);

	//UKismetSystemLibrary::PrintString(GetWorld(), TEXT("ALobbyGM::Login End"));

	return PC;
}

void ALobbyGM::PostLogin(APlayerController* NewPlayer)
{
	//UKismetSystemLibrary::PrintString(GetWorld(), TEXT("ALobbyGM::PostLogin Begin"));

	Super::PostLogin(NewPlayer);

	CountConnection();
	//UKismetSystemLibrary::PrintString(GetWorld(), TEXT("ALobbyGM::PostLogin End"));

}

void ALobbyGM::Logout(AController* Exiting)
{
	//UKismetSystemLibrary::PrintString(GetWorld(), TEXT("ALobbyGM::Logout Begin"));
	
	Super::Logout(Exiting);

	CountConnection();

	//UKismetSystemLibrary::PrintString(GetWorld(), TEXT("ALobbyGM::Logout End"));
}

void ALobbyGM::StartPlay()
{
	//UKismetSystemLibrary::PrintString(GetWorld(), TEXT("ALobbyGM::StartPlay Begin"));

	Super::StartPlay();

	if (GetNetMode() != NM_ListenServer)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("Skipping game server registration: Lobby World is unavailable"));
		return;
	}

	UNetDriver* NetDriver = World->GetNetDriver();
	if (NetDriver == nullptr || !NetDriver->GetLocalAddr().IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("Skipping game server registration: active NetDriver has no local address"));
		return;
	}

	const int32 ListenPort = NetDriver->GetLocalAddr()->GetPort();
	if (ListenPort < 1 || ListenPort > 65535)
	{
		UE_LOG(LogTemp, Warning, TEXT("Skipping game server registration: bound Listen port is invalid"));
		return;
	}

	UGameInstance* GameInstance = World->GetGameInstance();
	if (GameInstance == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("Skipping game server registration: GameInstance is unavailable"));
		return;
	}

	UDataGameInstanceSubsystem* Data = GameInstance->GetSubsystem<UDataGameInstanceSubsystem>();
	if (Data == nullptr || Data->ServerIP.TrimStartAndEnd().IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("Skipping game server registration: FastAPI address is empty"));
		return;
	}

	UWebApiSubsystem* WebApi = GameInstance->GetSubsystem<UWebApiSubsystem>();
	if (WebApi == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("Skipping game server registration: WebApiSubsystem is unavailable"));
		return;
	}

	WebApi->StartGameServerRegistration(Data->ServerIP, ListenPort);

	//UKismetSystemLibrary::PrintString(GetWorld(), TEXT("ALobbyGM::StartPlay End"));
}

void ALobbyGM::BeginPlay()
{
	Super::BeginPlay();

	
	GetWorld()->GetTimerManager().SetTimer(
		LeftTimeHandle,
		FTimerDelegate::CreateLambda([this]() {
			CountDownLeftTime();
		}),
		1.0f,
		true,
		0.0f
	);
}

void ALobbyGM::CountConnection()
{
	int Count = GetNumPlayers();
	//for (auto Iter = GetWorld()->GetPlayerControllerIterator(); Iter; ++Iter)
	//{
	//	Count++;
	//}

	ALobbyGS* GS = GetGameState<ALobbyGS>();
	if (GS)
	{
		GS->ConnectionCount = Count;

		//ReplicatedUsing이지만 C++에서는 호출이 안됨.
		GS->OnRep_ConnectionCount();
	}
}

void ALobbyGM::CountDownLeftTime()
{

	ALobbyGS* GS = GetGameState<ALobbyGS>();
	if (GS)
	{
		GS->LeftTime--;
		GS->LeftTime = FMath::Clamp(GS->LeftTime, 0, 60);

		//ReplicatedUsing이지만 C++에서는 호출이 안됨.
		GS->OnRep_LeftTime();

		if (GS->LeftTime <= 0)
		{
			StartGame();
		}
	}
}

void ALobbyGM::StopTimer()
{
	GetWorldTimerManager().ClearTimer(
		LeftTimeHandle
	);
}

void ALobbyGM::StartGame()
{
	StopTimer();
	for (auto Iter = GetWorld()->GetPlayerControllerIterator(); Iter; ++Iter)
	{
		ALobbyPC* PC = Cast<ALobbyPC>(*Iter);
		if (PC)
		{
			PC->S2C_ShowLoadingScreen();
		}
	}


	GetWorld()->ServerTravel(TEXT("Lvl_ThirdPerson"));


}
