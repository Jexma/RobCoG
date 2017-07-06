// Copyright 2017, Institute for Artificial Intelligence - University of Bremen
// Author: Andrei Haidu (http://haidu.eu)

#include "Hand.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "HandOrientationParser.h"
#include "Engine/Engine.h"

// Sets default values
AHand::AHand()
{
	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	// Fixation grasp parameters	
	bEnableFixationGrasp = true;
	bGraspHeld = false;
	bTwoHandGraspActive = false;
	OneHandFixationMaximumMass = 5.f;
	OneHandFixationMaximumLength = 50.f;
	TwoHandsFixationMaximumMass = 15.f;
	TwoHandsFixationMaximumLength = 120.f;

	// Set attachement collision component
	AttachmentCollision = CreateDefaultSubobject<USphereComponent>(TEXT("AttachmentCollision"));
	AttachmentCollision->SetupAttachment(GetRootComponent());
	AttachmentCollision->InitSphereRadius(4.f);

	// Set default as left hand
	HandType = EHandType::Left;

	// Set skeletal mesh default physics related values
	USkeletalMeshComponent* const SkelComp = GetSkeletalMeshComponent();
	SkelComp->SetSimulatePhysics(true);
	SkelComp->SetEnableGravity(false);
	SkelComp->SetCollisionProfileName(TEXT("BlockAll"));
	SkelComp->bGenerateOverlapEvents = true;

	// Angular drive default values
	AngularDriveMode = EAngularDriveMode::SLERP;
	Spring = 9000.0f;
	Damping = 1000.0f;
	ForceLimit = 0.0f;

	// Set fingers and their bone names default values
	AHand::SetupHandDefaultValues(HandType);

	// Set skeletal default values
	//AHand::SetupSkeletalDefaultValues(GetSkeletalMeshComponent());

	GraspPtr = MakeShareable(new Grasp());
}

// Called when the game starts or when spawned
void AHand::BeginPlay()
{
	Super::BeginPlay();

	AttachmentCollision->OnComponentBeginOverlap.AddDynamic(this, &AHand::OnAttachmentCollisionBeginOverlap);
	AttachmentCollision->OnComponentEndOverlap.AddDynamic(this, &AHand::OnAttachmentCollisionEndOverlap);

	// Setup the values for controlling the hand fingers
	AHand::SetupAngularDriveValues(AngularDriveMode);

}

// Called every frame, used for motion control
void AHand::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

// Update default values if properties have been changed in the editor
#if WITH_EDITOR
void AHand::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	// Call the base class version  
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Get the name of the property that was changed  
	FName PropertyName = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	// If hand type has been changed
	if ((PropertyName == GET_MEMBER_NAME_CHECKED(AHand, HandType)))
	{
		AHand::SetupHandDefaultValues(HandType);
	}

	// If the skeletal mesh has been changed
	if ((PropertyName == GET_MEMBER_NAME_CHECKED(AHand, GetSkeletalMeshComponent())))
	{
		//AHand::SetupSkeletalDefaultValues(GetSkeletalMeshComponent());
	}

	UE_LOG(LogTemp, Warning, TEXT("Selected property name: %s"), *PropertyName.ToString());
}
#endif  

// Object in reach for grasping
void AHand::OnAttachmentCollisionBeginOverlap(class UPrimitiveComponent* HitComp, class AActor* OtherActor,
	class UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult & SweepResult)
{
	if (!GraspedObject)
	{
		// Hand is free, check if object is graspable
		const uint8 Graspable = IsGraspable(OtherActor);

		if (Graspable == HAND_ONE)
		{
			OneHandGraspableObjects.Emplace(Cast<AStaticMeshActor>(OtherActor));
		}
		else if (Graspable == HAND_TWO)
		{
			PossibleTwoHandGraspObject = Cast<AStaticMeshActor>(OtherActor);
		}
	}
}

// Object out of reach for grasping
void AHand::OnAttachmentCollisionEndOverlap(class UPrimitiveComponent* HitComp, class AActor* OtherActor,
	class UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	// Object out of reach
	if (!GraspedObject)
	{
		// If present, remove from the graspable objects
		OneHandGraspableObjects.Remove(Cast<AStaticMeshActor>(OtherActor));
	}
	UE_LOG(LogTemp, Warning, TEXT("Overlap end: %s | GraspableObjects size: %i"),
		*OtherActor->GetName(), OneHandGraspableObjects.Num());

}

// Check if object is graspable
uint8 AHand::IsGraspable(AActor* InActor)
{
	// Check if the static mesh actor can be grasped
	AStaticMeshActor* const SMActor = Cast<AStaticMeshActor>(InActor);
	if (SMActor)
	{
		// Get the static mesh component
		UStaticMeshComponent* const SMComp = SMActor->GetStaticMeshComponent();
		if (SMComp)
		{
			// Check that actor is movable, and has a static mesh component with physics on
			const bool bIsLegalComponent = SMActor->IsRootComponentMovable() && SMComp->IsSimulatingPhysics();
			
			// Check that mass and size are within limits for a one hand grasp
			const float Mass = SMComp->GetMass();
			const float Length = SMActor->GetComponentsBoundingBox().GetSize().Size();
			const bool bOneHandGraspable = Mass < OneHandFixationMaximumMass && Length < OneHandFixationMaximumLength;

			if (bIsLegalComponent && bOneHandGraspable)
			{
				return HAND_ONE;
			}
			else if(Mass < TwoHandsFixationMaximumMass && Length < TwoHandsFixationMaximumLength)
			{
				return HAND_TWO;
			}
		}
	}
	// Actor cannot be attached
	return HAND_NONE;
}

// Hold grasp in the current position
void AHand::HoldGrasp()
{
	//for (const auto& ConstrMapItr : Thumb.FingerPartToConstraint)
	//{
	//	ConstrMapItr.Value->SetAngularOrientationTarget(FQuat(FRotator(
	//		ConstrMapItr.Value->GetCurrentSwing2(),
	//		ConstrMapItr.Value->GetCurrentSwing1(),
	//		ConstrMapItr.Value->GetCurrentTwist())));
	//}
	//for (const auto& ConstrMapItr : Index.FingerPartToConstraint)
	//{
	//	ConstrMapItr.Value->SetAngularOrientationTarget(FQuat(FRotator(
	//		ConstrMapItr.Value->GetCurrentSwing2(),
	//		ConstrMapItr.Value->GetCurrentSwing1(),
	//		ConstrMapItr.Value->GetCurrentTwist())));
	//}
	//for (const auto& ConstrMapItr : Middle.FingerPartToConstraint)
	//{
	//	ConstrMapItr.Value->SetAngularOrientationTarget(FQuat(FRotator(
	//		ConstrMapItr.Value->GetCurrentSwing2(),
	//		ConstrMapItr.Value->GetCurrentSwing1(),
	//		ConstrMapItr.Value->GetCurrentTwist())));
	//}
	//for (const auto& ConstrMapItr : Ring.FingerPartToConstraint)
	//{
	//	ConstrMapItr.Value->SetAngularOrientationTarget(FQuat(FRotator(
	//		ConstrMapItr.Value->GetCurrentSwing2(),
	//		ConstrMapItr.Value->GetCurrentSwing1(),
	//		ConstrMapItr.Value->GetCurrentTwist())));
	//}
	//for (const auto& ConstrMapItr : Pinky.FingerPartToConstraint)
	//{
	//	ConstrMapItr.Value->SetAngularOrientationTarget(FQuat(FRotator(
	//		ConstrMapItr.Value->GetCurrentSwing2(),
	//		ConstrMapItr.Value->GetCurrentSwing1(),
	//		ConstrMapItr.Value->GetCurrentTwist())));
	//}

	bGraspHeld = true;
}

// Setup hand default values
FORCEINLINE void AHand::SetupHandDefaultValues(EHandType InHandType)
{
	if (InHandType == EHandType::Left)
	{
		Thumb.FingerType = EFingerType::Thumb;
		Thumb.FingerPartToBoneName.Add(EFingerPart::Proximal, "thumb_01_l");
		Thumb.FingerPartToBoneName.Add(EFingerPart::Intermediate, "thumb_02_l");
		Thumb.FingerPartToBoneName.Add(EFingerPart::Distal, "thumb_03_l");

		Index.FingerType = EFingerType::Index;
		Index.FingerPartToBoneName.Add(EFingerPart::Proximal, "index_01_l");
		Index.FingerPartToBoneName.Add(EFingerPart::Intermediate, "index_02_l");
		Index.FingerPartToBoneName.Add(EFingerPart::Distal, "index_03_l");

		Middle.FingerType = EFingerType::Middle;
		Middle.FingerPartToBoneName.Add(EFingerPart::Proximal, "middle_01_l");
		Middle.FingerPartToBoneName.Add(EFingerPart::Intermediate, "middle_02_l");
		Middle.FingerPartToBoneName.Add(EFingerPart::Distal, "middle_03_l");

		Ring.FingerType = EFingerType::Ring;
		Ring.FingerPartToBoneName.Add(EFingerPart::Proximal, "ring_01_l");
		Ring.FingerPartToBoneName.Add(EFingerPart::Intermediate, "ring_02_l");
		Ring.FingerPartToBoneName.Add(EFingerPart::Distal, "ring_03_l");

		Pinky.FingerType = EFingerType::Pinky;
		Pinky.FingerPartToBoneName.Add(EFingerPart::Proximal, "pinky_01_l");
		Pinky.FingerPartToBoneName.Add(EFingerPart::Intermediate, "pinky_02_l");
		Pinky.FingerPartToBoneName.Add(EFingerPart::Distal, "pinky_03_l");
	}
	else if (InHandType == EHandType::Right)
	{
		Thumb.FingerType = EFingerType::Thumb;
		Thumb.FingerPartToBoneName.Add(EFingerPart::Proximal, "thumb_01_r");
		Thumb.FingerPartToBoneName.Add(EFingerPart::Intermediate, "thumb_02_r");
		Thumb.FingerPartToBoneName.Add(EFingerPart::Distal, "thumb_03_r");

		Index.FingerType = EFingerType::Index;
		Index.FingerPartToBoneName.Add(EFingerPart::Proximal, "index_01_r");
		Index.FingerPartToBoneName.Add(EFingerPart::Intermediate, "index_02_r");
		Index.FingerPartToBoneName.Add(EFingerPart::Distal, "index_03_r");

		Middle.FingerType = EFingerType::Middle;
		Middle.FingerPartToBoneName.Add(EFingerPart::Proximal, "middle_01_r");
		Middle.FingerPartToBoneName.Add(EFingerPart::Intermediate, "middle_02_r");
		Middle.FingerPartToBoneName.Add(EFingerPart::Distal, "middle_03_r");

		Ring.FingerType = EFingerType::Ring;
		Ring.FingerPartToBoneName.Add(EFingerPart::Proximal, "ring_01_r");
		Ring.FingerPartToBoneName.Add(EFingerPart::Intermediate, "ring_02_r");
		Ring.FingerPartToBoneName.Add(EFingerPart::Distal, "ring_03_r");

		Pinky.FingerType = EFingerType::Pinky;
		Pinky.FingerPartToBoneName.Add(EFingerPart::Proximal, "pinky_01_r");
		Pinky.FingerPartToBoneName.Add(EFingerPart::Intermediate, "pinky_02_r");
		Pinky.FingerPartToBoneName.Add(EFingerPart::Distal, "pinky_03_r");
	}
}

// Setup skeletal mesh default values
FORCEINLINE void AHand::SetupSkeletalDefaultValues(USkeletalMeshComponent* InSkeletalMeshComponent)
{
	if (InSkeletalMeshComponent->GetPhysicsAsset())
	{
		// Hand joint velocity drive
		InSkeletalMeshComponent->SetAllMotorsAngularPositionDrive(true, true);

		// Set drive parameters
		InSkeletalMeshComponent->SetAllMotorsAngularDriveParams(Spring, Damping, ForceLimit);

		UE_LOG(LogTemp, Error, TEXT("AHand: SkeletalMeshComponent's angular motors set!"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("AHand: SkeletalMeshComponent's has no PhysicsAsset set!"));
	}
}

// Setup fingers angular drive values
FORCEINLINE void AHand::SetupAngularDriveValues(EAngularDriveMode::Type DriveMode)
{
	USkeletalMeshComponent* const SkelMeshComp = GetSkeletalMeshComponent();
	if (Thumb.SetFingerPartsConstraints(SkelMeshComp->Constraints))
	{
		Thumb.SetFingerDriveMode(DriveMode, Spring, Damping, ForceLimit);
	}
	if (Index.SetFingerPartsConstraints(SkelMeshComp->Constraints))
	{
		Index.SetFingerDriveMode(DriveMode, Spring, Damping, ForceLimit);
	}
	if (Middle.SetFingerPartsConstraints(SkelMeshComp->Constraints))
	{
		Middle.SetFingerDriveMode(DriveMode, Spring, Damping, ForceLimit);
	}
	if (Ring.SetFingerPartsConstraints(SkelMeshComp->Constraints))
	{
		Ring.SetFingerDriveMode(DriveMode, Spring, Damping, ForceLimit);
	}
	if (Pinky.SetFingerPartsConstraints(SkelMeshComp->Constraints))
	{
		Pinky.SetFingerDriveMode(DriveMode, Spring, Damping, ForceLimit);
	}
}

// Switch the grasp pose
void AHand::SwitchGrasp()
{
	//TODO: Dynamically Switching, not hardcoded

	//IHandOrientationReadable* HandOrientationReadable = Cast<IHandOrientationReadable>(HandOrientationParser);
	if (GraspPtr.IsValid())
	{
		GraspPtr->SwitchGrasp(this);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Grasp shared pointer is not valid!"));
	}
}

// Update the grasp pose
void AHand::UpdateGrasp(const float Goal)
{
		if (!GraspedObject)
		{
			for (const auto& ConstrMapItr : Thumb.FingerPartToConstraint)
			{
				ConstrMapItr.Value->SetAngularOrientationTarget(FQuat(FRotator(0.f, 0.f, Goal * 100.f)));
			}
			for (const auto& ConstrMapItr : Index.FingerPartToConstraint)
			{
				ConstrMapItr.Value->SetAngularOrientationTarget(FQuat(FRotator(0.f, 0.f, Goal * 100.f)));
			}
			for (const auto& ConstrMapItr : Middle.FingerPartToConstraint)
			{
				ConstrMapItr.Value->SetAngularOrientationTarget(FQuat(FRotator(0.f, 0.f, Goal * 100.f)));
			}
			for (const auto& ConstrMapItr : Ring.FingerPartToConstraint)
			{
				ConstrMapItr.Value->SetAngularOrientationTarget(FQuat(FRotator(0.f, 0.f, Goal * 100.f)));
			}
			for (const auto& ConstrMapItr : Pinky.FingerPartToConstraint)
			{
				ConstrMapItr.Value->SetAngularOrientationTarget(FQuat(FRotator(0.f, 0.f, Goal * 100.f)));
			}
		}
		else if (!bGraspHeld)
		{
			AHand::HoldGrasp();
		}
}

void AHand::UpdateGrasp2(const float Alpha)
{
	GraspPtr->UpdateGrasp(Alpha, this);
}

// Attach grasped object to hand
bool AHand::AttachToHand()
{
	if ((!GraspedObject) && (OneHandGraspableObjects.Num() > 0))
	{
		GraspedObject = OneHandGraspableObjects.Pop();
		
		// Check if the other hand is currently grasping the object
		if (GraspedObject->GetAttachParentActor() && GraspedObject->GetAttachParentActor()->IsA(AHand::StaticClass()))
		{
			AHand* OtherHand = Cast<AHand>(GraspedObject->GetAttachParentActor());			
			UE_LOG(LogTemp, Warning, TEXT("AHand: Attached %s to %s from %s"), *GraspedObject->GetName(), *GetName(), *OtherHand->GetName());
			OtherHand->DetachFromHand();
		}
		GraspedObject->GetStaticMeshComponent()->SetSimulatePhysics(false);
		GraspedObject->AttachToComponent(GetRootComponent(), FAttachmentTransformRules(
			EAttachmentRule::KeepWorld, EAttachmentRule::KeepWorld, EAttachmentRule::KeepWorld, true));
		//GraspedObject->AttachToActor(this, FAttachmentTransformRules(
		//	EAttachmentRule::KeepWorld, EAttachmentRule::KeepWorld, EAttachmentRule::KeepWorld, true));
		UE_LOG(LogTemp, Warning, TEXT("AHand: Attached %s to %s"), *GraspedObject->GetName(), *GetName());

		return true;
	}
	return false;
}

// Detach grasped object from hand
bool AHand::DetachFromHand()
{
	if (GraspedObject)
	{
		GraspedObject->GetStaticMeshComponent()->DetachFromComponent(FDetachmentTransformRules(
			EDetachmentRule::KeepWorld, EDetachmentRule::KeepWorld, EDetachmentRule::KeepWorld, true));
		UE_LOG(LogTemp, Warning, TEXT("AHand: Detached %s from %s"), *GraspedObject->GetName(), *GetName());
		GraspedObject->GetStaticMeshComponent()->SetSimulatePhysics(true);
		GraspedObject->GetStaticMeshComponent()->SetPhysicsLinearVelocity(GetVelocity());
		GraspedObject = nullptr;
		bGraspHeld = false;
		return true;
	}
	else if (bTwoHandGraspActive && PossibleTwoHandGraspObject)
	{
		PossibleTwoHandGraspObject->GetStaticMeshComponent()->DetachFromComponent(FDetachmentTransformRules(
			EDetachmentRule::KeepWorld, EDetachmentRule::KeepWorld, EDetachmentRule::KeepWorld, true));
		UE_LOG(LogTemp, Warning, TEXT("AHand: Detached %s from %s"), *PossibleTwoHandGraspObject->GetName(), *GetName());
		PossibleTwoHandGraspObject->GetStaticMeshComponent()->SetSimulatePhysics(true);
		PossibleTwoHandGraspObject->GetStaticMeshComponent()->SetPhysicsLinearVelocity(GetVelocity());
		PossibleTwoHandGraspObject = nullptr;
		bTwoHandGraspActive = false;
		return true;
	}
	return false;
}

// Detach grasped object from hand
bool AHand::TwoHandAttach()
{
	if (PossibleTwoHandGraspObject)
	{
		PossibleTwoHandGraspObject->GetStaticMeshComponent()->SetSimulatePhysics(false);
		PossibleTwoHandGraspObject->AttachToComponent(GetRootComponent(), FAttachmentTransformRules(
			EAttachmentRule::KeepWorld, EAttachmentRule::KeepWorld, EAttachmentRule::KeepWorld, true));
		bTwoHandGraspActive = true;
		return true;
	}
	return false;
}

