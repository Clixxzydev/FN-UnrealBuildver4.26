// Copyright Epic Games, Inc. All Rights Reserved.

#include "HLOD/HLODProxyMesh.h"

FHLODProxyMesh::FHLODProxyMesh()
	: StaticMesh(nullptr)
{
}

#if WITH_EDITOR
FHLODProxyMesh::FHLODProxyMesh(const FHLODProxyMesh& Other)
	: StaticMesh(Other.StaticMesh)
	, Key(Other.Key)
{
}

FHLODProxyMesh::FHLODProxyMesh(ALODActor* InLODActor, UStaticMesh* InStaticMesh, const FName& InKey)
	: LODActor(InLODActor)
	, StaticMesh(InStaticMesh)
	, Key(InKey)
{
}

FHLODProxyMesh::FHLODProxyMesh(UStaticMesh* InStaticMesh, const FName& InKey)
	: StaticMesh(InStaticMesh)
	, Key(InKey)
{
}

bool FHLODProxyMesh::operator==(const FHLODProxyMesh& InHLODProxyMesh) const
{
	return LODActor == InHLODProxyMesh.LODActor &&
		   StaticMesh == InHLODProxyMesh.StaticMesh &&
		   Key == InHLODProxyMesh.Key;
}
#endif

const UStaticMesh* FHLODProxyMesh::GetStaticMesh() const
{
	return StaticMesh;
}

const TLazyObjectPtr<ALODActor>& FHLODProxyMesh::GetLODActor() const
{
	return LODActor;
}

const FName& FHLODProxyMesh::GetKey() const
{
	return Key;
}
