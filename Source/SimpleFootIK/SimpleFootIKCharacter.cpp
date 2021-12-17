// Copyright Epic Games, Inc. All Rights Reserved.

#include "SimpleFootIKCharacter.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/SecureHash.h"

//////////////////////////////////////////////////////////////////////////
// ASimpleFootIKCharacter

ASimpleFootIKCharacter::ASimpleFootIKCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

	// set our turn rates for input
	BaseTurnRate = 45.f;
	BaseLookUpRate = 45.f;

	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true; // Character moves in the direction of input...	
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 540.0f, 0.0f); // ...at this rotation rate
	GetCharacterMovement()->JumpZVelocity = 600.f;
	GetCharacterMovement()->AirControl = 0.2f;

	// Create a camera boom (pulls in towards the player if there is a collision)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 300.0f; // The camera follows at this distance behind the character	
	CameraBoom->bUsePawnControlRotation = true; // Rotate the arm based on the controller

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName); // Attach the camera to the end of the boom and let the boom adjust to match the controller orientation
	FollowCamera->bUsePawnControlRotation = false; // Camera does not rotate relative to arm

	// Note: The skeletal mesh and anim blueprint references on the Mesh component (inherited from Character) 
	// are set in the derived blueprint asset named MyCharacter (to avoid direct content references in C++)

	CapsuleHalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    IKTraceDistance = CapsuleHalfHeight / 2;
}

//////////////////////////////////////////////////////////////////////////
// Input

void ASimpleFootIKCharacter::SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent)
{
	// Set up gameplay key bindings
	check(PlayerInputComponent);
	PlayerInputComponent->BindAction("Jump", IE_Pressed, this, &ACharacter::Jump);
	PlayerInputComponent->BindAction("Jump", IE_Released, this, &ACharacter::StopJumping);

	PlayerInputComponent->BindAxis("MoveForward", this, &ASimpleFootIKCharacter::MoveForward);
	PlayerInputComponent->BindAxis("MoveRight", this, &ASimpleFootIKCharacter::MoveRight);

	// We have 2 versions of the rotation bindings to handle different kinds of devices differently
	// "turn" handles devices that provide an absolute delta, such as a mouse.
	// "turnrate" is for devices that we choose to treat as a rate of change, such as an analog joystick
	PlayerInputComponent->BindAxis("Turn", this, &APawn::AddControllerYawInput);
	PlayerInputComponent->BindAxis("TurnRate", this, &ASimpleFootIKCharacter::TurnAtRate);
	PlayerInputComponent->BindAxis("LookUp", this, &APawn::AddControllerPitchInput);
	PlayerInputComponent->BindAxis("LookUpRate", this, &ASimpleFootIKCharacter::LookUpAtRate);

	// handle touch devices
	PlayerInputComponent->BindTouch(IE_Pressed, this, &ASimpleFootIKCharacter::TouchStarted);
	PlayerInputComponent->BindTouch(IE_Released, this, &ASimpleFootIKCharacter::TouchStopped);

	// VR headset functionality
	PlayerInputComponent->BindAction("ResetVR", IE_Pressed, this, &ASimpleFootIKCharacter::OnResetVR);
}

void ASimpleFootIKCharacter::OnResetVR()
{
	// If SimpleFootIK is added to a project via 'Add Feature' in the Unreal Editor the dependency on HeadMountedDisplay in SimpleFootIK.Build.cs is not automatically propagated
	// and a linker error will result.
	// You will need to either:
	//		Add "HeadMountedDisplay" to [YourProject].Build.cs PublicDependencyModuleNames in order to build successfully (appropriate if supporting VR).
	// or:
	//		Comment or delete the call to ResetOrientationAndPosition below (appropriate if not supporting VR)
	UHeadMountedDisplayFunctionLibrary::ResetOrientationAndPosition();
}

void ASimpleFootIKCharacter::TouchStarted(ETouchIndex::Type FingerIndex, FVector Location)
{
		Jump();
}

void ASimpleFootIKCharacter::TouchStopped(ETouchIndex::Type FingerIndex, FVector Location)
{
		StopJumping();
}

void ASimpleFootIKCharacter::TurnAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerYawInput(Rate * BaseTurnRate * GetWorld()->GetDeltaSeconds());
}

void ASimpleFootIKCharacter::LookUpAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerPitchInput(Rate * BaseLookUpRate * GetWorld()->GetDeltaSeconds());
}

void ASimpleFootIKCharacter::MoveForward(float Value)
{
	if ((Controller != nullptr) && (Value != 0.0f))
	{
		// find out which way is forward
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector Direction = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		AddMovementInput(Direction, Value);
	}
}

void ASimpleFootIKCharacter::MoveRight(float Value)
{
	if ( (Controller != nullptr) && (Value != 0.0f) )
	{
		// find out which way is right
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);
	
		// get right vector 
		const FVector Direction = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
		// add movement in that direction
		AddMovementInput(Direction, Value);
	}
}

void ASimpleFootIKCharacter::IK()
{
	float HitDistanceL = GetHitDistance(LeftFootSocket);
    float HitDistanceR = GetHitDistance(RightFootSocket);

    if( HitDistanceL < 0 || HitDistanceR < 0 )
    {
        IKOffsetLeftFoot = FInterp(IKOffsetLeftFoot, 0);
        IKOffsetRightFoot = FInterp(IKOffsetRightFoot, 0);
    }
    else
    {
        float HitDistanceDifference = FMath::Abs(HitDistanceL - HitDistanceR);
        if( HitDistanceL < HitDistanceR )
        {
            IKOffsetLeftFoot = FInterp(IKOffsetLeftFoot, HitDistanceDifference);
            IKOffsetRightFoot = FInterp(IKOffsetRightFoot, 0);
        }
        else
        {
            IKOffsetLeftFoot = FInterp(IKOffsetLeftFoot, 0);
            IKOffsetRightFoot = FInterp(IKOffsetRightFoot, HitDistanceDifference);
        }
    }

    float MeshOffsetZMax = FMath::Max(HitDistanceL, HitDistanceR);
    MeshOffsetZ = FInterp(MeshOffsetZ, MeshOffsetZMax);

	float _CapsuleHalfHeight = FInterp(CapsuleHalfHeight, CapsuleHalfHeight - MeshOffsetZMax / 2);
	GetCapsuleComponent()->SetCapsuleHalfHeight(_CapsuleHalfHeight);
}

float ASimpleFootIKCharacter::GetHitDistance(FName Socket)
{
	float HitDistance = IKFootTrace(Socket);
    GEngine->AddOnScreenDebugMessage(FCString::Atoi(*FMD5::HashAnsiString(*Socket.ToString())), 5.f, FColor::White, FString::Printf(TEXT("The %s is: %f"), *Socket.ToString(), HitDistance));
    return HitDistance;
}

float ASimpleFootIKCharacter::IKFootTrace(FName Socket)
{
	FVector Start;
    FVector End;

    FVector SocketLocation = GetMesh()->GetSocketLocation(Socket);
    float X = SocketLocation.X;
    float Y = SocketLocation.Y;
    float FootZ = GetActorLocation().Z - CapsuleHalfHeight;
    Start.Set(X, Y, FootZ);
    End.Set(X, Y, FootZ - IKTraceDistance);

    TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
    ObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_WorldStatic));
    TArray<AActor*> ActorsToIgnore;
    FHitResult OutHit;

    UKismetSystemLibrary::LineTraceSingleForObjects(
        GetWorld(),
        Start,
        End,
        ObjectTypes,
        false,
        ActorsToIgnore,
        EDrawDebugTrace::ForOneFrame,
        OutHit,
        true
    );

    float HitDistance;
    if( OutHit.bBlockingHit )
    {
        HitDistance = OutHit.Distance;
    }
    else
    {
        HitDistance = -1;
    }

    return HitDistance;
}

float ASimpleFootIKCharacter::FInterp(float CurrentValue, float TargetValue)
{
	return FMath::FInterpTo(CurrentValue, TargetValue, GetWorld()->GetDeltaSeconds(), IKInterpSpeed);
}