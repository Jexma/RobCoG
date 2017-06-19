// Copyright 2017, Institute for Artificial Intelligence - University of Bremen
// Author: Andrei Haidu (http://haidu.eu)

#include "MCCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "Components/CapsuleComponent.h"
#include "Components/ArrowComponent.h"
#include "Components/InputComponent.h"
#include "HeadMountedDisplay.h"
#include "IHeadMountedDisplay.h"

// Sets default values
AMCCharacter::AMCCharacter(const FObjectInitializer& ObjectInitializer)
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	// Set this pawn to be controlled by the lowest-numbered player
	AutoPossessPlayer = EAutoReceiveInput::Player0;
	// Make the capsule thin, and set it only to collide with static objects (VR Mode)
	GetCapsuleComponent()->SetCapsuleRadius(10);
	GetCapsuleComponent()->SetCollisionProfileName(TEXT("Spectator"));

	// Set flag default values
	bShowTargetArrows = true;
	bUseHandsInitialRotationAsOffset = true;

	// Create the motion controller offset (hands in front of the character), attach to root component
	MCOriginComponent = CreateDefaultSubobject<USceneComponent>(TEXT("MCOriginComponent"));
	MCOriginComponent->SetupAttachment(GetRootComponent());
	MCOriginComponent->SetRelativeLocation(FVector(0.0f, 0.0f, -GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));

	// Create a CameraComponent, attach to capsule
	CharCamera = ObjectInitializer.CreateDefaultSubobject<UCameraComponent>(this, TEXT("MCCharacterCamera"));
	CharCamera->SetupAttachment(MCOriginComponent);
	// Default camera for VR use -- relative location at the floor 
	//CharCamera->SetRelativeLocation(FVector(0.0f, 0.0f, -GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
	//CharCamera->SetRelativeLocation(FVector(0.0f, 0.0f, BaseEyeHeight));

	// Create left/right motion controller
	MCLeft = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("MCLeft"));
	MCLeft->SetupAttachment(MCOriginComponent);
	MCLeft->Hand = EControllerHand::Left;
	MCRight = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("MCRight"));
	MCRight->SetupAttachment(MCOriginComponent);
	MCRight->Hand = EControllerHand::Right;

	// Create left/right target vis arrows, attached to the MC
	LeftTargetArrow = CreateDefaultSubobject<UArrowComponent>(TEXT("MCLeftTargetArrow"));
	LeftTargetArrow->ArrowSize = 0.1;
	LeftTargetArrow->SetupAttachment(MCLeft);
	RightTargetArrow = CreateDefaultSubobject<UArrowComponent>(TEXT("MCRightTargetArrow"));
	RightTargetArrow->ArrowSize = 0.1;
	RightTargetArrow->SetupAttachment(MCRight);

	// PID params
	PGain = 700.0f;
	IGain = 0.0f;
	DGain = 50.0f;
	MaxOutput = 350000.0f;
	RotationBoost = 12000.f;

	// Init rotation offset
	LeftHandRotationOffset = FQuat::Identity;
	RightHandRotationOffset = FQuat::Identity;
}

// Called when the game starts or when spawned
void AMCCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Set target arrow visibility at runtime
	LeftTargetArrow->SetHiddenInGame(!bShowTargetArrows);
	RightTargetArrow->SetHiddenInGame(!bShowTargetArrows);

	// Set the hand PID controller values
	LeftPIDController.SetValues(PGain, IGain, DGain, MaxOutput, -MaxOutput);
	RightPIDController.SetValues(PGain, IGain, DGain, MaxOutput, -MaxOutput);

	// Check if VR is enabled
	IHeadMountedDisplay* HMD = (IHeadMountedDisplay*)(GEngine->HMDDevice.Get());
	if (HMD && HMD->IsStereoEnabled())
	{		
		// VR MODE
		//CharCamera->SetRelativeLocation(FVector(0.0f, 0.0f, -GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
	}
	else
	{		
		GetCapsuleComponent()->SetCollisionProfileName(TEXT("Pawn"));
		CharCamera->SetRelativeLocation(FVector(0.0f, 0.0f, BaseEyeHeight));
		CharCamera->bUsePawnControlRotation = true;
		MCOriginComponent->SetRelativeLocation(FVector(70.f, 0.f, -10.f));
		MCLeft->SetRelativeLocation(FVector(0.f, -30.f, 0.f));
		MCRight->SetRelativeLocation(FVector(0.f, 30.f, 0.f));
	}

	// Cast the hands to MCHand
	MCLeftHand = Cast<AMCHand>(LeftHand);
	MCRightHand = Cast<AMCHand>(RightHand);

	// Set hand offsets
	if (bUseHandsInitialRotationAsOffset)
	{
		if (LeftHand)
		{
			LeftHandRotationOffset = LeftHand->GetSkeletalMeshComponent()->GetComponentQuat();
		}
		if (RightHand)
		{
			RightHandRotationOffset = RightHand->GetSkeletalMeshComponent()->GetComponentQuat();
		}
	}
}

// Called every frame
void AMCCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Move hands to target location and rotation
	if (LeftHand)
	{
		AMCCharacter::UpdateHandLocationAndRotation(
			MCLeft, LeftHandRotationOffset, LeftHand->GetSkeletalMeshComponent(), LeftPIDController, DeltaTime);
	}
	if (RightHand)
	{
		AMCCharacter::UpdateHandLocationAndRotation(
			MCRight, RightHandRotationOffset, RightHand->GetSkeletalMeshComponent(), RightPIDController, DeltaTime);
	}
}

// Called to bind functionality to input
void AMCCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// Set up gameplay key bindings
	PlayerInputComponent->BindAxis("MoveForward", this, &AMCCharacter::MoveForward);
	PlayerInputComponent->BindAxis("MoveRight", this, &AMCCharacter::MoveRight);
	// Default Camera view bindings
	PlayerInputComponent->BindAxis("CameraPitch", this, &AMCCharacter::AddControllerPitchInput);
	PlayerInputComponent->BindAxis("CameraYaw", this, &AMCCharacter::AddControllerYawInput);
	// Hand control binding
	PlayerInputComponent->BindAxis("GraspWithLeftHand", this, &AMCCharacter::GraspWithLeftHand);
	PlayerInputComponent->BindAxis("GraspWithRightHand", this, &AMCCharacter::GraspWithRightHand);

	// Hand action binding
	PlayerInputComponent->BindAction("AttachToLeftHand", IE_Pressed, this, &AMCCharacter::AttachToLeftHand);
	PlayerInputComponent->BindAction("AttachToRightHand", IE_Pressed, this, &AMCCharacter::AttachToRightHand);
}

// Handles moving forward/backward
void AMCCharacter::MoveForward(const float Value)
{
	if ((Controller != nullptr) && (Value != 0.0f))
	{
		// find out which way is forward
		FRotator Rotation = Controller->GetControlRotation();
		// Limit pitch when walking or falling
		if (GetCharacterMovement()->IsMovingOnGround() || GetCharacterMovement()->IsFalling())
		{
			Rotation.Pitch = 0.0f;
		}
		// add movement in that direction
		const FVector Direction = FRotationMatrix(Rotation).GetScaledAxis(EAxis::X);
		AddMovementInput(Direction, Value);
	}
}

// Handles moving right/left
void AMCCharacter::MoveRight(const float Value)
{
	if ((Controller != nullptr) && (Value != 0.0f))
	{
		// find out which way is right
		const FRotator Rotation = Controller->GetControlRotation();
		const FVector Direction = FRotationMatrix(Rotation).GetScaledAxis(EAxis::Y);
		// add movement in that direction
		AddMovementInput(Direction, Value);
	}
}

// Update hand positions
FORCEINLINE void AMCCharacter::UpdateHandLocationAndRotation(
	UMotionControllerComponent* MC,
	const FQuat& RotOffset,
	USkeletalMeshComponent* SkelMesh,
	PIDController3D& PIDController,
	const float DeltaTime)
{
	//// Location
	const FVector Error = MC->GetComponentLocation() - SkelMesh->GetComponentLocation();
	const FVector LocOutput = PIDController.UpdateAsPD(Error, DeltaTime);
	SkelMesh->AddForceToAllBodiesBelow(LocOutput, NAME_None, true, true);
	//// Velocity based control
	//const FVector LocOutput = PIDController.UpdateAsP(Error, DeltaTime);
	//SkelMesh->SetAllPhysicsLinearVelocity(LocOutput);

	//// Rotation
	const FQuat TargetQuat = MC->GetComponentQuat() * RotOffset;
	FQuat CurrQuat = SkelMesh->GetComponentQuat();

	// Dot product to get cos theta
	const float CosTheta = TargetQuat | CurrQuat;
	// Avoid taking the long path around the sphere
	if (CosTheta < 0)
	{
		CurrQuat *= -1.f;
	}
	// Use the xyz part of the quat as the rotation velocity
	const FQuat OutputFromQuat = TargetQuat * CurrQuat.Inverse();
	const FVector RotOutput = FVector(OutputFromQuat.X, OutputFromQuat.Y, OutputFromQuat.Z) * RotationBoost;
	SkelMesh->SetAllPhysicsAngularVelocity(RotOutput);
}

// Update left hand grasp
void AMCCharacter::GraspWithLeftHand(const float Val)
{
	if (MCLeftHand)
	{
		MCLeftHand->UpdateGrasp(Val);
	}
}

// Update right hand grasp
void AMCCharacter::GraspWithRightHand(const float Val)
{
	if (MCRightHand)
	{
		MCRightHand->UpdateGrasp(Val);
	}
}

// Attach to left hand
void AMCCharacter::AttachToLeftHand()
{
	if (MCLeftHand)
	{
		MCLeftHand->AttachToHand();
	}
}

// Attach to right hand
void AMCCharacter::AttachToRightHand()
{
	if (MCRightHand)
	{
		MCRightHand->AttachToHand();
	}
}