/*     Copyright 2015-2018 Egor Yusov
 *  
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF ANY PROPRIETARY RIGHTS.
 *
 *  In no event and under no legal theory, whether in tort (including negligence), 
 *  contract, or otherwise, unless required by applicable law (such as deliberate 
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental, 
 *  or consequential damages of any character arising as a result of this License or 
 *  out of the use or inability to use the software (including but not limited to damages 
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and 
 *  all other commercial damages or losses), even if such Contributor has been advised 
 *  of the possibility of such damages.
 */

#include "pch.h"

#include "ShaderVariableVk.h"

namespace Diligent
{

// Creates shader variable for every resource from SrcLayout whose type is one AllowedVarTypes
void ShaderVariableManagerVk::Initialize(const ShaderResourceLayoutVk& SrcLayout, 
                                         IMemoryAllocator&             Allocator,
                                         const SHADER_VARIABLE_TYPE*   AllowedVarTypes, 
                                         Uint32                        NumAllowedTypes, 
                                         ShaderResourceCacheVk&        ResourceCache)
{
    m_pResourceLayout = &SrcLayout;
    m_pResourceCache = &ResourceCache;
#ifdef _DEBUG
    m_pDbgAllocator = &Allocator;
#endif

    VERIFY_EXPR(m_NumVariables == 0);
    m_NumVariables = 0;
    Uint32 AllowedTypeBits = GetAllowedTypeBits(AllowedVarTypes, NumAllowedTypes);
    for(SHADER_VARIABLE_TYPE VarType = SHADER_VARIABLE_TYPE_STATIC; VarType < SHADER_VARIABLE_TYPE_NUM_TYPES; VarType = static_cast<SHADER_VARIABLE_TYPE>(VarType+1))
    {
        m_NumVariables += IsAllowedType(VarType, AllowedTypeBits) ? SrcLayout.GetResourceCount(VarType) : 0;
    }

    auto *pRawMem = ALLOCATE(Allocator, "Raw memory buffer for shader variables", m_NumVariables*sizeof(ShaderVariableVkImpl));
    m_pVariables = reinterpret_cast<ShaderVariableVkImpl*>(pRawMem);

    Uint32 VarInd = 0;
    for(SHADER_VARIABLE_TYPE VarType = SHADER_VARIABLE_TYPE_STATIC; VarType < SHADER_VARIABLE_TYPE_NUM_TYPES; VarType = static_cast<SHADER_VARIABLE_TYPE>(VarType+1))
    {
        if( !IsAllowedType(VarType, AllowedTypeBits))
            continue;

        Uint32 NumResources = SrcLayout.GetResourceCount(VarType);
        for( Uint32 r=0; r < NumResources; ++r )
        {
            const auto &SrcRes = SrcLayout.GetResource(VarType, r);
            ::new (m_pVariables + VarInd) ShaderVariableVkImpl(*this, SrcRes );
            ++VarInd;
        }
    }
    VERIFY_EXPR(VarInd == m_NumVariables);
}

ShaderVariableManagerVk::~ShaderVariableManagerVk()
{
    VERIFY(m_pVariables == nullptr, "Destroy() has not been called");
}

void ShaderVariableManagerVk::Destroy(IMemoryAllocator &Allocator)
{
    VERIFY(m_pDbgAllocator == &Allocator, "Incosistent alloctor");

    for(Uint32 v=0; v < m_NumVariables; ++v)
        m_pVariables[v].~ShaderVariableVkImpl();
    Allocator.Free(m_pVariables);
    m_pVariables = nullptr;
}

ShaderVariableVkImpl* ShaderVariableManagerVk::GetVariable(const Char* Name)
{
    ShaderVariableVkImpl* pVar = nullptr;
    for (Uint32 v = 0; v < m_NumVariables; ++v)
    {
        auto &Var = m_pVariables[v];
        const auto& Res = Var.m_Resource;
        if (strcmp(Res.SpirvAttribs.Name, Name) == 0)
        {
            pVar = &Var;
            break;
        }
    }
    return pVar;
}



void ShaderVariableManagerVk::BindResources( IResourceMapping* pResourceMapping, Uint32 Flags)
{
    VERIFY_EXPR(m_pResourceCache != nullptr);

    if( !pResourceMapping )
    {
        LOG_ERROR_MESSAGE( "Failed to bind resources: resource mapping is null" );
        return;
    }

    for(Uint32 v=0; v < m_NumVariables; ++v)
    {
        auto &Var = m_pVariables[v];
        const auto& Res = Var.m_Resource;
        
        // Skip immutable separate samplers
        if(Res.SpirvAttribs.Type == SPIRVShaderResourceAttribs::ResourceType::SeparateSampler && Res.SpirvAttribs.StaticSamplerInd >= 0)
            continue;

        for(Uint32 ArrInd = 0; ArrInd < Res.SpirvAttribs.ArraySize; ++ArrInd)
        {
            if( Flags & BIND_SHADER_RESOURCES_RESET_BINDINGS )
                Res.BindResource(nullptr, ArrInd, *m_pResourceCache);

            if( (Flags & BIND_SHADER_RESOURCES_UPDATE_UNRESOLVED) && Res.IsBound(ArrInd, *m_pResourceCache) )
                continue;

            const auto* VarName = Res.SpirvAttribs.Name;
            RefCntAutoPtr<IDeviceObject> pObj;
            pResourceMapping->GetResource( VarName, &pObj, ArrInd );
            if( pObj )
            {
                Res.BindResource(pObj, ArrInd, *m_pResourceCache);
            }
            else
            {
                if( (Flags & BIND_SHADER_RESOURCES_ALL_RESOLVED) && !Res.IsBound(ArrInd, *m_pResourceCache) )
                    LOG_ERROR_MESSAGE( "Cannot bind resource to shader variable \"", Res.SpirvAttribs.GetPrintName(ArrInd), "\": resource view not found in the resource mapping" );
            }
        }
    }
}

}
